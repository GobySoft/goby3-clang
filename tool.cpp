// Declares clang::SyntaxOnlyAction.
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
// Declares llvm::cl::extrahelp.
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <iostream>
#include <sstream>

using namespace clang::tooling;
using namespace llvm;
using namespace clang;
using namespace clang::ast_matchers;

//

template <const char* const* method>
StatementMatcher pubsub_matcher = cxxMemberCallExpr(
    expr().bind("pubsub_call_expr"),
    // call is on an instantiation of a class derived from StaticTransporterInterface
    on(expr(
        anyOf(
            // Thread: pull out what "this" in "this->interprocess()" when "this" is derived from goby::middleware::Thread
            cxxMemberCallExpr(on(hasType(pointsTo(cxxRecordDecl(
                cxxRecordDecl().bind("on_thread_decl"),
                isDerivedFrom(cxxRecordDecl(hasName("::goby::middleware::Thread")))))))),
            // also allow out direct calls to publish/subscribe
            expr().bind("on_expr")),
        hasType(cxxRecordDecl(
            decl().bind("on_type_decl"),
            isDerivedFrom(cxxRecordDecl(hasName("::goby::middleware::StaticTransporterInterface"))),
            unless(hasName("::goby::middleware::NullTransporter")))))),
    callee(cxxMethodDecl(
        // "publish" or "subscribe"
        hasName(*method),
        // Group (must refer to goby::middleware::Group)
        hasTemplateArgument(0, templateArgument(refersToDeclaration(varDecl(
                                   hasType(cxxRecordDecl(hasName("::goby::middleware::Group"))),
                                   // find the actual string argument and bind it
                                   hasDescendant(cxxConstructExpr(hasArgument(
                                       0, stringLiteral().bind("group_string_arg")))))))),
        // Type (no restrictions)
        hasTemplateArgument(1, templateArgument().bind("type_arg")),
        // Scheme (must be int)
        hasTemplateArgument(2,
                            templateArgument(templateArgument().bind("scheme_arg"),
                                             refersToIntegralType(qualType(asString("int"))))))));

class PubSubPrinter : public MatchFinder::MatchCallback
{
  public:
    virtual void run(const MatchFinder::MatchResult& Result)
    {
        // the function call itself (e.g. interprocess().publish<...>(...));
        const auto* pubsub_call_expr =
            Result.Nodes.getNodeAs<clang::CXXMemberCallExpr>("pubsub_call_expr");

        const auto* group_string_lit =
            Result.Nodes.getNodeAs<clang::StringLiteral>("group_string_arg");
        const auto* type_arg = Result.Nodes.getNodeAs<clang::TemplateArgument>("type_arg");
        const auto* scheme_arg = Result.Nodes.getNodeAs<clang::TemplateArgument>("scheme_arg");
        const auto* on_type_decl = Result.Nodes.getNodeAs<clang::CXXRecordDecl>("on_type_decl");
        //        const auto* on_expr = Result.Nodes.getNodeAs<clang::Expr>("on_expr");
        const auto* on_thread_decl = Result.Nodes.getNodeAs<clang::CXXRecordDecl>("on_thread_decl");

        if (!pubsub_call_expr || !group_string_lit || !type_arg || !scheme_arg || !on_type_decl)
            return;

        const std::string layer_type = on_type_decl->getQualifiedNameAsString();
        std::string layer = "unknown";

        if (layer_type.find("InterThread") != std::string::npos)
            layer = "interthread";
        else if (layer_type.find("InterProcess") != std::string::npos)
            layer = "interprocess";
        else if (layer_type.find("InterVehicle") != std::string::npos)
            layer = "intervehicle";

        //        if(on_thread_decl)
        //            on_thread_decl->dump();

        std::string thread = "unknown";
        if (on_thread_decl)
        {
            // todo: see if there's a cleaner way to get this with the template parameters
            thread = on_thread_decl->getTypeForDecl()->getCanonicalTypeInternal().getAsString();
            std::string class_str = "class ";
            if(thread.find(class_str) != std::string::npos)
                thread = thread.substr(class_str.size());
        }

        const std::string group = group_string_lit->getString().str();
        const std::string type = type_arg->getAsType().getAsString();
        const int scheme = scheme_arg->getAsIntegral().getExtValue();

        // hide internal groups for now
        if (group.find("goby::") != std::string::npos)
            return;

        std::cout << name() << " | thread: " << thread << ", layer: " << layer
                  << ", group: " << group << ", scheme: " << scheme << ", type: " << type
                  << std::endl;
    }

  private:
    virtual std::string name() = 0;
};

class PublishPrinter : public PubSubPrinter
{
  private:
    std::string name() override { return "publish"; }
};

class SubscribePrinter : public PubSubPrinter
{
  private:
    std::string name() override { return "subscribe"; }
};

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static cl::OptionCategory Goby3ToolCategory("goby3-tool options");

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

constexpr const char* publish_str = "publish";
constexpr const char* subscribe_str = "subscribe";

int main(int argc, const char** argv)
{
    CommonOptionsParser OptionsParser(argc, argv, Goby3ToolCategory);
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());

    PublishPrinter publish_printer;
    SubscribePrinter subscribe_printer;
    MatchFinder finder;

    finder.addMatcher(pubsub_matcher<&publish_str>, &publish_printer);
    finder.addMatcher(pubsub_matcher<&subscribe_str>, &subscribe_printer);

    return Tool.run(newFrontendActionFactory(&finder).get());
}
