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

StatementMatcher LoopMatcher =
    forStmt(
        hasLoopInit(declStmt(hasSingleDecl(varDecl(hasInitializer(integerLiteral(equals(0))))))))
        .bind("forLoop");

class LoopPrinter : public MatchFinder::MatchCallback
{
  public:
    virtual void run(const MatchFinder::MatchResult& Result)
    {
        if (const ForStmt* FS = Result.Nodes.getNodeAs<clang::ForStmt>("forLoop"))
            FS->dump();
    }
};

StatementMatcher publish_matcher = cxxMemberCallExpr(
    // call is on an instantiation of a class derived from StaticTransporterInterface
    on(hasType(cxxRecordDecl(
        isDerivedFrom(cxxRecordDecl(hasName("::goby::middleware::StaticTransporterInterface")))))),
    // callee is publish, with a template argument of an instantiation of Group
    callee(cxxMethodDecl(hasName("publish"),
                         hasAnyTemplateArgument(refersToDeclaration(
                             varDecl(hasType(cxxRecordDecl(hasName("::goby::middleware::Group"))),
                                     // find the actual string argument and bind it
                                     hasDescendant(cxxConstructExpr(hasArgument(
                                         0, stringLiteral().bind("group_string_arg"))))))))));

StatementMatcher subscribe_matcher = cxxMemberCallExpr(
    // call is on an instantiation of a class derived from StaticTransporterInterface
    on(hasType(cxxRecordDecl(
        isDerivedFrom(cxxRecordDecl(hasName("::goby::middleware::StaticTransporterInterface")))))),
    // callee is subscribe, with a template argument of an instantiation of Group
    callee(cxxMethodDecl(hasName("subscribe"),
                         hasAnyTemplateArgument(refersToDeclaration(
                             varDecl(hasType(cxxRecordDecl(hasName("::goby::middleware::Group"))),
                                     // find the actual string argument and bind it
                                     hasDescendant(cxxConstructExpr(hasArgument(
                                         0, stringLiteral().bind("group_string_arg"))))))))));


class PublishPrinter : public MatchFinder::MatchCallback
{
  public:
    virtual void run(const MatchFinder::MatchResult& Result)
    {
        if (const auto* a = Result.Nodes.getNodeAs<clang::StringLiteral>("group_string_arg"))
        {
            //            std::cout << "Template Decl Constructor Argument: " << std::endl;
            // a->dump();
            std::cout << "Publish: " << a->getString().str() << std::endl;
        }
    }
};

class SubscribePrinter : public MatchFinder::MatchCallback
{
  public:
    virtual void run(const MatchFinder::MatchResult& Result)
    {
        if (const auto* a = Result.Nodes.getNodeAs<clang::StringLiteral>("group_string_arg"))
        {
            //            std::cout << "Template Decl Constructor Argument: " << std::endl;
            // a->dump();
            std::cout << "Subscribe: " << a->getString().str() << std::endl;
        }
    }
};


// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static cl::OptionCategory Goby3ToolCategory("goby3-tool options");

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static cl::extrahelp MoreHelp("\nMore help text...\n");

int main(int argc, const char** argv)
{
    CommonOptionsParser OptionsParser(argc, argv, Goby3ToolCategory);
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());

    //  LoopPrinter Printer;
    PublishPrinter publish_printer;
    SubscribePrinter subscribe_printer;
    MatchFinder finder;
    //  Finder.addMatcher(LoopMatcher, &Printer);
    finder.addMatcher(publish_matcher, &publish_printer);
    finder.addMatcher(subscribe_matcher, &subscribe_printer);

    return Tool.run(newFrontendActionFactory(&finder).get());
}
