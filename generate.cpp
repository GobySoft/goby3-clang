#include <fstream>
#include <iostream>
#include <sstream>

#include "yaml_raii.h"
#include <yaml-cpp/yaml.h>

#include "goby/middleware/marshalling/interface.h"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"

#include "actions.h"
#include "pubsub_entry.h"

using goby::clang::Layer;
using goby::clang::PubSubEntry;

std::map<Layer, std::string> layer_to_str{{Layer::UNKNOWN, "unknown"},
                                          {Layer::INTERTHREAD, "interthread"},
                                          {Layer::INTERPROCESS, "interprocess"},
                                          {Layer::INTERVEHICLE, "intervehicle"}};

::clang::ast_matchers::StatementMatcher pubsub_matcher(const char* method)
{
    using namespace clang::ast_matchers;
    return cxxMemberCallExpr(
        expr().bind("pubsub_call_expr"),
        // call is on an instantiation of a class derived from StaticTransporterInterface
        on(expr(
            anyOf(
                // Thread: pull out what "this" in "this->interprocess()" when "this" is derived from goby::middleware::Thread
                cxxMemberCallExpr(on(hasType(pointsTo(cxxRecordDecl(
                    cxxRecordDecl().bind("on_thread_decl"),
                    isDerivedFrom(cxxRecordDecl(hasName("::goby::middleware::Thread")))))))),
                // Thread: match this->goby().interprocess() or similar chains
                hasDescendant(cxxMemberCallExpr(on(hasType(pointsTo(cxxRecordDecl(
                    cxxRecordDecl().bind("on_indirect_thread_decl"),
                    isDerivedFrom(cxxRecordDecl(hasName("::goby::middleware::Thread"))))))))),
                // also allow out direct calls to publish/subscribe
                expr().bind("on_expr")),
            hasType(cxxRecordDecl(decl().bind("on_type_decl"),
                                  isDerivedFrom(cxxRecordDecl(
                                      hasName("::goby::middleware::StaticTransporterInterface"))),
                                  unless(hasName("::goby::middleware::NullTransporter")))))),
        callee(cxxMethodDecl(
            // "publish" or "subscribe"
            hasName(method),
            // Group (must refer to goby::middleware::Group)
            hasTemplateArgument(0, templateArgument(refersToDeclaration(varDecl(
                                       hasType(cxxRecordDecl(hasName("::goby::middleware::Group"))),
                                       // find the actual string argument and bind it
                                       hasDescendant(cxxConstructExpr(hasArgument(
                                           0, stringLiteral().bind("group_string_arg")))))))),
            // Type (no restrictions)
            hasTemplateArgument(1, templateArgument().bind("type_arg")),
            // Scheme (must be int)
            hasTemplateArgument(
                2, templateArgument(templateArgument().bind("scheme_arg"),
                                    refersToIntegralType(qualType(asString("int"))))))));
}

class PubSubAggregator : public ::clang::ast_matchers::MatchFinder::MatchCallback
{
  public:
    virtual void run(const ::clang::ast_matchers::MatchFinder::MatchResult& Result)
    {
        // the function call itself (e.g. interprocess().publish<...>(...));
        const auto* pubsub_call_expr =
            Result.Nodes.getNodeAs<clang::CXXMemberCallExpr>("pubsub_call_expr");

        const auto* group_string_lit =
            Result.Nodes.getNodeAs<clang::StringLiteral>("group_string_arg");
        const auto* type_arg = Result.Nodes.getNodeAs<clang::TemplateArgument>("type_arg");
        const auto* scheme_arg = Result.Nodes.getNodeAs<clang::TemplateArgument>("scheme_arg");
        const auto* on_type_decl = Result.Nodes.getNodeAs<clang::CXXRecordDecl>("on_type_decl");
        const auto* on_thread_decl = Result.Nodes.getNodeAs<clang::CXXRecordDecl>("on_thread_decl");

        if (!on_thread_decl)
            on_thread_decl =
                Result.Nodes.getNodeAs<clang::CXXRecordDecl>("on_indirect_thread_decl");

        if (!pubsub_call_expr || !group_string_lit || !type_arg || !scheme_arg || !on_type_decl)
            return;

        const std::string layer_type = on_type_decl->getQualifiedNameAsString();
        Layer layer = Layer::UNKNOWN;

        if (layer_type.find("InterThread") != std::string::npos)
            layer = Layer::INTERTHREAD;
        else if (layer_type.find("InterProcess") != std::string::npos)
            layer = Layer::INTERPROCESS;
        else if (layer_type.find("InterVehicle") != std::string::npos)
            layer = Layer::INTERVEHICLE;

        std::string thread = "unknown";
        std::set<std::string> bases;
        if (on_thread_decl)
        {
            thread = as_string(*on_thread_decl);
            for (auto it = on_thread_decl->bases_begin(), end = on_thread_decl->bases_end();
                 it != end; ++it)
                bases.insert(as_string(*(it->getType()->getAsCXXRecordDecl())));
        }
        bases_[thread] = bases;

        const std::string group = group_string_lit->getString().str();
        const std::string type = as_string(*type_arg->getAsType());
        const int scheme_num = scheme_arg->getAsIntegral().getExtValue();
        const std::string scheme = goby::middleware::MarshallingScheme::to_string(scheme_num);

        // hide internal groups for now
        if (group.find("goby::") != std::string::npos)
            return;

        entries_.emplace(layer, thread, group, scheme, type);
    }

    const std::set<PubSubEntry>& entries() const { return entries_; }
    const std::set<std::string>& bases(const std::string& thread) { return bases_[thread]; }

  private:    
    std::string as_string(const clang::Type& type)
    {
        // todo: see if there's a cleaner way to get this with the template parameters
        std::string str(type.getCanonicalTypeInternal().getAsString());

        // remove class, struct
        std::string class_str = "class ";
        if (str.find(class_str) == 0)
            str = str.substr(class_str.size());

        std::string struct_str = "struct ";
        if (str.find(struct_str) == 0)
            str = str.substr(struct_str.size());

        return str;
    }

    std::string as_string(const clang::CXXRecordDecl& cxx_decl)
    {
        return as_string(*(cxx_decl.getTypeForDecl()));
    }

  private:
    std::set<PubSubEntry> entries_;
    // map thread to bases
    std::map<std::string, std::set<std::string>> bases_;
};

int goby::clang::generate(::clang::tooling::ClangTool& Tool, std::string output_directory,
                          std::string output_file, std::string target_name)
{
    PubSubAggregator publish_aggregator, subscribe_aggregator;
    ::clang::ast_matchers::MatchFinder finder;

    finder.addMatcher(pubsub_matcher("publish"), &publish_aggregator);
    finder.addMatcher(pubsub_matcher("subscribe"), &subscribe_aggregator);

    if (output_file.empty())
        output_file = target_name + "_interface.yml";

    std::string file_name(output_directory + "/" + output_file);
    std::ofstream ofs(file_name.c_str());
    if (!ofs.is_open())
    {
        std::cerr << "Failed to open " << file_name << " for writing" << std::endl;
        exit(EXIT_FAILURE);
    }

    auto retval = Tool.run(::clang::tooling::newFrontendActionFactory(&finder).get());

    std::set<Layer> layers_in_use;
    std::set<std::string> threads_in_use;
    for (const auto& e : publish_aggregator.entries())
    {
        layers_in_use.insert(e.layer);
        threads_in_use.insert(e.thread);
    }
    for (const auto& e : subscribe_aggregator.entries())
    {
        layers_in_use.insert(e.layer);
        threads_in_use.insert(e.thread);
    }

    // intervehicle requires interprocess at this point
    if (layers_in_use.count(Layer::INTERVEHICLE))
        layers_in_use.insert(Layer::INTERPROCESS);

    YAML::Emitter yaml_out;
    {
        goby::yaml::YMap root_map(yaml_out);
        root_map.add("application", std::string(target_name));

        // put inner most layer last
        for (auto layer_it = layers_in_use.rbegin(), end = layers_in_use.rend(); layer_it != end;
             ++layer_it)
        {
            Layer layer = *layer_it;
            root_map.add_key(layer_to_str.at(layer));
            goby::yaml::YMap layer_map(yaml_out);

            auto emit_pub_sub = [&](goby::yaml::YMap& map, const std::string& thread) {
                {
                    map.add_key("publishes");
                    goby::yaml::YSeq publish_seq(yaml_out);
                    for (const auto& e : publish_aggregator.entries())
                    {
                        // show inner publications
                        if (e.layer >= layer && (layer != Layer::INTERTHREAD || e.thread == thread))
                            e.write_yaml_map(yaml_out, layer != Layer::INTERTHREAD,
                                             e.layer > layer);
                    }
                }

                {
                    map.add_key("subscribes");
                    goby::yaml::YSeq subscribe_seq(yaml_out);
                    for (const auto& e : subscribe_aggregator.entries())
                    {
                        if (e.layer == layer && (layer != Layer::INTERTHREAD || e.thread == thread))
                            e.write_yaml_map(yaml_out, layer != Layer::INTERTHREAD);
                    }
                }
            };

            if (layer == Layer::INTERTHREAD)
            {
                layer_map.add_key("threads");
                goby::yaml::YSeq thread_seq(yaml_out);
                for (const auto& thread : threads_in_use)
                {
                    goby::yaml::YMap thread_map(yaml_out);
                    {
                        thread_map.add("name", thread);

                        const auto& pub_bases = publish_aggregator.bases(thread);
                        const auto& sub_bases = subscribe_aggregator.bases(thread);
                        auto bases(pub_bases);
                        bases.insert(sub_bases.begin(), sub_bases.end());
                        if (!bases.empty())
                        {
                            thread_map.add_key("bases");
                            goby::yaml::YSeq bases_seq(yaml_out);
                            for (const auto& base : bases) bases_seq.add(base);
                        }

                        emit_pub_sub(thread_map, thread);
                    }
                }
            }
            else
            {
                emit_pub_sub(layer_map, "");
            }
        }
    }

    ofs << yaml_out.c_str();

    return retval;
}
