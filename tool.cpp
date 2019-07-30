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

StatementMatcher PublishMatcher =
    cxxMemberCallExpr(
        on(hasType(
            cxxRecordDecl(isDerivedFrom(cxxRecordDecl(hasName("StaticTransporterInterface")))))),
        callee(cxxMethodDecl(hasName("publish"),
                             hasAnyTemplateArgument(refersToDeclaration(
                                 varDecl(hasType(cxxRecordDecl(hasName("::Group"))),
                                         hasDescendant(cxxConstructExpr().bind("constructExpr")))
                                     .bind("group_template_decl"))))
                   .bind("publishDecl")))
        .bind("publishCall");

class PublishPrinter : public MatchFinder::MatchCallback
{
  public:
    virtual void run(const MatchFinder::MatchResult& Result)
    {
        // if (const auto* e = Result.Nodes.getNodeAs<clang::CXXMemberCallExpr>("publishCall"))
        // {
        //     std::cout << "Call:" << std::endl;
        //     e->dump();
        // }

        // if (const auto* d = Result.Nodes.getNodeAs<clang::CXXMethodDecl>("publishDecl"))
        // {
        //     std::cout << "Decl:" << std::endl;
        //     d->dump();

        //     FunctionTemplateSpecializationInfo* tpl_info = d->getTemplateSpecializationInfo();
        //     if (tpl_info)
        //     {
        //         const TemplateArgumentList* tpl_list = tpl_info->TemplateArguments;
        //         std::cout << "Has " << tpl_list->size() << " template arguments" << std::endl;
        //     }
        // }

        // if (const auto* t = Result.Nodes.getNodeAs<clang::VarDecl>("group_template_decl"))
        // {
        //     std::cout << "Template Decl: " << std::endl;
        //     t->dump();
        // }

        if (const auto* c = Result.Nodes.getNodeAs<clang::CXXConstructExpr>("constructExpr"))
        {
            std::cout << "Template Decl Constructor: " << std::endl;
            c->dump();
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
    PublishPrinter Printer;
    MatchFinder Finder;
    //  Finder.addMatcher(LoopMatcher, &Printer);
    Finder.addMatcher(PublishMatcher, &Printer);

    return Tool.run(newFrontendActionFactory(&Finder).get());
}
