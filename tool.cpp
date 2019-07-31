// Declares clang::SyntaxOnlyAction.
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
// Declares llvm::cl::extrahelp.
#include "llvm/Support/CommandLine.h"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <iostream>

using namespace clang::tooling;
using namespace llvm;
using namespace clang;
using namespace clang::ast_matchers;

template <const char* const* method>
StatementMatcher pubsub_matcher =
    cxxMemberCallExpr(
        // call is on an instantiation of a class derived from StaticTransporterInterface
        on(expr(hasType(cxxRecordDecl(isDerivedFrom(cxxRecordDecl(hasName(
                                          "::goby::middleware::StaticTransporterInterface"))),
                                      unless(hasName("::goby::middleware::NullTransporter")))
                            .bind("on_type")))
               .bind("on_expr")),
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
            hasTemplateArgument(2, templateArgument(refersToIntegralType(qualType(asString("int"))))
                                       .bind("scheme_arg")))))
        .bind("pubsub_call_expr");

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
        const auto* on_type = Result.Nodes.getNodeAs<clang::CXXRecordDecl>("on_type");
        const auto* on_expr = Result.Nodes.getNodeAs<clang::Expr>("on_expr");

        if (!pubsub_call_expr || !group_string_lit || !type_arg || !scheme_arg || !on_type)
            return;

        const std::string layer_type = on_type->getQualifiedNameAsString();
        std::string layer = "unknown";

        if (layer_type.find("InterThread") != std::string::npos)
            layer = "interthread";
        else if (layer_type.find("InterProcess") != std::string::npos)
            layer = "interprocess";
        else if (layer_type.find("InterVehicle") != std::string::npos)
            layer = "intervehicle";

        const std::string group = group_string_lit->getString().str();
        const std::string type = type_arg->getAsType().getAsString();
        const int scheme = scheme_arg->getAsIntegral().getExtValue();

        if (on_expr)
        {
            std::cout << "on_expr:" << std::endl;
            on_expr->dump();
            // std::cout << "on_expr callee:" << std::endl;
            // on_expr->getCallee()->dump();
            // std::cout << "on_expr method decl:" << std::endl;
            // on_expr->getMethodDecl()->dump();
            // std::cout << "on_expr record decl:" << std::endl;
            // on_expr->getRecordDecl()->dump();
        }

        std::cout << name() << " | layer: " << layer << ", group: " << group
                  << ", scheme: " << scheme << ", type: " << type << std::endl;
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
