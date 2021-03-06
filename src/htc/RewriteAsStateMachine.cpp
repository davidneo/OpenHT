/* Copyright (c) 2015 Convey Computer Corporation
 *
 * This file is part of the OpenHT toolset located at:
 *
 * https://github.com/TonyBrewer/OpenHT
 *
 * Use and distribution licensed under the BSD 3-clause license.
 * See the LICENSE file for the complete license text.
 */

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include "HtcAttributes.h"
#include "HtDeclManager.h"
#include "HtdInfoAttribute.h"
#include "ProcessStencils.h"
#include "RewriteAsStateMachine.h"
#include "HtSageUtils.h"

#define foreach BOOST_FOREACH  // Replace with range-based for when C++'11 

extern bool debugHooks;


//
//
class RewriteFSM : public AstSimpleProcessing {
private:
  void visit(SgNode *S) {
    switch (S->variantT()) {
    case V_SgFunctionDeclaration:
      visitSgFunctionDeclaration(dynamic_cast<SgFunctionDeclaration *>(S));
      break;
    case V_SgGlobal:
      if (debugHooks) {
        SageInterface::insertHeader("assert.h", PreprocessingInfo::after,
            true, isSgGlobal(S));
      }
      break;
    default:
      break;
    }
  }

  void visitSgFunctionDeclaration(SgFunctionDeclaration *FD);
};


static std::string debugHookString =
"\n#undef mystate \n"
"#undef PR_htValid \n"
"#undef PR_htId \n"
"#undef PR_htInst \n"
"#undef HtContinue \n"
"#undef ReadMemPause \n"
"#undef WriteMemPause \n"
"#undef HtPause \n"
"#undef HtRetry \n"
"#undef HtAssert \n"
"#undef ReadMemBusy \n"
"#undef WriteMemBusy \n"
"#define mystate		htc_statenum \n"
"#define PR_htValid		1 \n"
"#define PR_htId		1 \n"
"#define PR_htInst		mystate \n"
"#define HtContinue(x)		mystate = (x); \n"
"#define ReadMemPause(x)	mystate = (x); \n"
"#define WriteMemPause(x)	mystate = (x); \n"
"#define HtPause()		mystate = mystate; \n"
"#define HtRetry()		mystate = mystate; \n"
"#define HtAssert(_C_, _V_)	assert((_C_)); \n"
"#define ReadMemBusy()          0\n"
"#define WriteMemBusy()         0\n"
"int htc_statenum = 1; \n"
"while(1) \n";


bool isHtThreadControlStmt(SgStatement *S)
{
  SgExprStatement *es = isSgExprStatement(S);
  SgFunctionCallExp *fce = 0;
  if (es && (fce = isSgFunctionCallExp(es->get_expression()))) {
    SgFunctionDeclaration *calleeFD = fce->getAssociatedFunctionDeclaration();
    std::string fname = calleeFD->get_name().getString();
    size_t pos = 0;
    if (fname == "WriteMemPause" ||
        fname == "ReadMemPause" ||
        fname == "HtBarrier" ||
        (((pos = fname.find("SendCall_")) != std::string::npos
         /*  || (pos = fname.find("SendCallFork_")) != std::string::npos */
           || (pos = fname.find("SendReturn_")) != std::string::npos
           || (pos = fname.find("RecvReturnJoin_")) != std::string::npos)
         && pos == 0)) {
      return true;
    }
  }
  return false;
}


void RewriteFSM::visitSgFunctionDeclaration(SgFunctionDeclaration *FD)
{
  SgFunctionDefinition *fdef = FD->get_definition();
  if (!fdef) {
    return;
  }

  if (debugHooks) {
    std::cout << "Func decl: " << FD << " " << FD->get_name() << std::endl;
  }

  std::string modName = FD->get_name().getString();
  HtdInfoAttribute *htd = getHtdInfoAttribute(fdef);

  bool isStreamingStencil = false;
  size_t pos = 0;
  if ((pos = modName.find(StencilStreamPrefix)) != std::string::npos
      && pos == 0) {
    isStreamingStencil = true;
  }

#define hostEntryPrefix  "__HTC_HOST_ENTRY_"
  bool isHostEntry = false;
  if ((pos = modName.find(hostEntryPrefix)) != std::string::npos
      && pos == 0) {
    isHostEntry = true;
  }

  // Emit a default, unnamed thread group.
  std::string modWidth = boost::to_upper_copy(modName) + "_HTID_W";
  if (isStreamingStencil) {
    // The streaming version of a stencil must have width 0.
    htd->appendDefine(modWidth, "0");
  } else if (isHostEntry) {
    htd->appendDefine(modWidth, "1");
  } else if (htd->moduleWidth != -1) {
    htd->appendDefine(modWidth, boost::lexical_cast<std::string>(htd->moduleWidth));
  } else {
    DefaultModuleWidthAttribute *dwAttr = 
        getDefaultModuleWidthAttribute(SageInterface::getGlobalScope(fdef));
    if (dwAttr) {
      htd->appendDefine(modWidth, boost::lexical_cast<std::string>(dwAttr->width));
    } else { 
      htd->appendDefine(modWidth, "5");
    }
  }
  htd->appendModule(modName, "", modWidth);

  // For streaming stencils, ProcessStencils inserts a canned sequence,
  // so we bypass generating a normal FSM.
  if (isStreamingStencil) {
    return;
  }

  //
  // Create new case body blocks for each state.
  // The first executable statement starts the first state, and each
  // label starts a new state.
  //
  std::map<SgLabelStatement *, int> labelToState;
  std::map<int, std::string> stateToName;
  SgBasicBlock *funcBody = isSgBasicBlock(fdef->get_body());
  SgStatementPtrList &stmts = funcBody->get_statements();

  std::vector<SgStatement *>::iterator SI, SP;
  for (SI = stmts.begin(); SI != stmts.end(); ++SI) {
    if (!isSgDeclarationStatement(*SI)) {
      break;
    }
  }
  if (SI == stmts.end()) {
    return;
  }
  SP = SI;

  std::vector<SgBasicBlock *> newBlocks;
  SgBasicBlock *newbb = SageBuilder::buildBasicBlock();
  newBlocks.push_back(newbb);
  stateToName[1] = "__START";
  bool prevIsLabel = false;
  for (; SI != stmts.end(); ++SI) {
    SgStatement *stmt = *SI;
    SgLabelStatement *labstmt = isSgLabelStatement(stmt);
    if (labstmt) {
      if (!prevIsLabel) {
        newbb = SageBuilder::buildBasicBlock();
        newBlocks.push_back(newbb);
      }
      int snum = newBlocks.size();
      labelToState[labstmt] = snum;
      stateToName[snum] += "__" + labstmt->get_label().getString();
      prevIsLabel = true;
#if 1
      // TODO: these labels can carry preproc infos-- but the unparser
      // doesn't output them if the label is not actually output.
      AttachedPreprocessingInfoType *comments =
          labstmt->getAttachedPreprocessingInfo();
      if (comments && comments->size() > 0) {
        std::cerr << "DEVWARN: losing Preprocinfo on label" << std::endl;
        SageInterface::dumpPreprocInfo(labstmt);
      }
#endif
      stmt->unsetOutputInCodeGeneration();
      SageInterface::appendStatement(stmt, newbb);
    } else {
      prevIsLabel = false;
      SageInterface::appendStatement(stmt, newbb);
    }
  }
  stmts.erase(SP, stmts.end());

  // Add module name to each state name and create enum decl.
  SgEnumDeclaration *enumDecl = SageBuilder::buildEnumDeclaration("states",
    fdef);
  for (int i = 1; i <= newBlocks.size(); i++) {
    stateToName[i] = modName + stateToName[i];
    boost::to_upper(stateToName[i]);
    SgName nm(stateToName[i]);
    SgInitializedName *enumerator = SageBuilder::buildInitializedName(nm,
        SageBuilder::buildIntType(),
        SageBuilder::buildAssignInitializer(SageBuilder::buildIntVal(i)));
    enumerator->set_scope(funcBody);
    enumDecl->append_enumerator(enumerator);

    // Add the instruction to the htd info.
    htd->appendInst(nm.getString());
  }
  SageInterface::prependStatement(enumDecl, funcBody);
  if (!debugHooks) {
    enumDecl->unsetOutputInCodeGeneration();
  }

  SgGlobal *GS = SageInterface::getGlobalScope(FD);
  SgVariableDeclaration *declHtValid = 
      HtDeclMgr::buildHtlVarDecl("PR_htValid", GS);
  SgVariableDeclaration *declHtInst = 
      HtDeclMgr::buildHtlVarDecl("PR_htInst", GS);
  SgFunctionDeclaration *declHtContinue = 
      HtDeclMgr::buildHtlFuncDecl("HtContinue", GS);
  SgFunctionDeclaration *declHtAssert =
     HtDeclMgr::buildHtlFuncDecl("HtAssert", GS);

  //
  // Create the finite state machine switch statement "switch (PR_htInst)",
  // and insert guard "if (PR_htValid)".
  //
  SgBasicBlock *newSwitchBody = SageBuilder::buildBasicBlock();

  SgExpression *htInstExpr = SageBuilder::buildVarRefExp(declHtInst);
  SgSwitchStatement *newSwitch = 
      SageBuilder::buildSwitchStatement(htInstExpr, newSwitchBody);

  SgExpression *htValidExpr = SageBuilder::buildVarRefExp(declHtValid);
  SgIfStmt *newIfStmt = SageBuilder::buildIfStmt(htValidExpr,
      SageBuilder::buildBasicBlock(newSwitch), 0);
  SageInterface::appendStatement(newIfStmt, funcBody);

  int casenum = 1;
  foreach (SgBasicBlock *newCaseBody, newBlocks) {
    SgExpression *caseExpr = 
        SageBuilder::buildEnumVal_nfi(casenum, enumDecl, stateToName[casenum]);
    SgCaseOptionStmt *newCase = 
        SageBuilder::buildCaseOptionStmt(caseExpr, newCaseBody);
    SageInterface::appendStatement(newCase, newSwitchBody);
    casenum++;
  }
  SgExprStatement *callStmt = SageBuilder::buildFunctionCallStmt("HtAssert",
      SageBuilder::buildVoidType(), SageBuilder::buildExprListExp(
      SageBuilder::buildIntVal(0), SageBuilder::buildIntVal(0)), funcBody);
  SgStatement *brkStmt = SageBuilder::buildBreakStmt();
  SageInterface::appendStatement(SageBuilder::buildDefaultOptionStmt(
      SageBuilder::buildBasicBlock(callStmt, brkStmt)), newSwitchBody);

  //
  // Process each new FSM state.
  // Insert HtContinue state transitions for each explicit (goto) or
  // implicit (fall-through) transition.
  //
  casenum = 1;
  foreach (SgBasicBlock *caseBody, newBlocks) {
    SgStatementPtrList &stmts = caseBody->get_statements();
    SgStatement *lastStmt = 0;
    if (!stmts.empty()) {
#if 0
      lastStmt = stmts.back();
#else
      // Skip NullStatements.
      for (int idx = stmts.size() - 1; idx >= 0; idx--) {
        if (!isSgNullStatement(stmts[idx])) {
          lastStmt = stmts[idx];
          break;
        }
      }
#endif
    }

    // Insert HtContinue for a fall-through.
    bool fallsThrough = (lastStmt == 0 || !isSgGotoStatement(lastStmt));
    if (fallsThrough) {
      if (!isSgSwitchStatement(lastStmt) && !isHtThreadControlStmt(lastStmt)) {
        int target = (casenum == newBlocks.size()) ? -1 : casenum + 1;
        callStmt = SageBuilder::buildFunctionCallStmt(
            "HtContinue", SageBuilder::buildVoidType(),
            SageBuilder::buildExprListExp(SageBuilder::buildEnumVal_nfi(target,
            enumDecl, stateToName[target])), funcBody);
        SageInterface::appendStatement(callStmt, caseBody);
      }
      SgStatement *brkStmt = SageBuilder::buildBreakStmt();
      SageInterface::appendStatement(brkStmt, caseBody);
    }

    // Insert HtContinue for each explicit goto.
    std::vector<SgGotoStatement *> gotos = 
        SageInterface::querySubTree<SgGotoStatement>(caseBody,
        V_SgGotoStatement); 
    foreach (SgGotoStatement *ngoto, gotos) {
      int target = labelToState[ngoto->get_label()];
      callStmt = SageBuilder::buildFunctionCallStmt(
          "HtContinue", SageBuilder::buildVoidType(),
          SageBuilder::buildExprListExp(SageBuilder::buildEnumVal_nfi(target,
          enumDecl, stateToName[target])), funcBody);
      SgStatement *brkStmt = SageBuilder::buildBreakStmt();
      SageInterface::insertStatementBefore(ngoto, callStmt);
      SageInterface::replaceStatement(ngoto, brkStmt, true);
    }

    // Insert break for RecvReturnPause, which acts like an explicit goto.
    // That is, control is transferred to a new state (like HtContinue),
    // and we must break out of the case.  Likewise for SendReturn_foo.
    // We also need to do this for WriteMemPause.
    std::vector<SgFunctionCallExp *> callExprs = 
        SageInterface::querySubTree<SgFunctionCallExp>(caseBody,
        V_SgFunctionCallExp); 
    foreach (SgFunctionCallExp *nfce, callExprs) {
      SgFunctionDeclaration *CFD = nfce->getAssociatedFunctionDeclaration();
      std::string fname = CFD->get_name().getString();
      size_t pos = 0;
      if (((pos = fname.find("RecvReturnPause_")) != std::string::npos)
          || ((pos = fname.find("WriteMemPause")) != std::string::npos 
              && lastStmt != nfce->get_parent())
          || ((pos = fname.find("SendReturn_")) != std::string::npos 
              && lastStmt != nfce->get_parent())
          && pos == 0) {
        SgStatement *brkStmt = SageBuilder::buildBreakStmt();
        SgStatement *parent = isSgStatement(nfce->get_parent());
        SageInterface::insertStatementAfter(parent, brkStmt);
      }
    }

    // Rewrite any FSM placeholder expressions.
    std::vector<SgExpression *> deferredExprs = 
        SageInterface::querySubTree<SgExpression>(caseBody, V_SgExpression); 
    foreach (SgExpression *nexpr, deferredExprs) {
      FsmPlaceHolderAttribute *fsmAttr = getFsmPlaceHolderAttribute(nexpr);
      if (!fsmAttr) {
        continue;
      }
      int target = labelToState[fsmAttr->targetLabel];
      SgExpression *enVal = 
          SageBuilder::buildEnumVal_nfi(target, enumDecl, stateToName[target]);
      SageInterface::replaceExpression(nexpr, enVal, false /* keepOldExp */);
    }

    casenum++;
  }

  // If user asked for debugging hooks, emit them just in front of
  // the PR_htValid guard.
  if (debugHooks) {
    SageInterface::addTextForUnparser(newIfStmt, ("\n" + htd->debugHookStr),
        AstUnparseAttribute::e_before);
    SageInterface::addTextForUnparser(newIfStmt, debugHookString,
        AstUnparseAttribute::e_before);
  }

}


static bool switchIsFSM(SgSwitchStatement *S)
{
  SgExprStatement *se = 
    isSgExprStatement(isSgSwitchStatement(S)->get_item_selector());
  SgVarRefExp *vref = 0;
  if (se && (vref = isSgVarRefExp(se->get_expression()))) {
    SgName switchName = vref->get_symbol()->get_name();
    if (switchName.getString() == "PR_htInst") {
      return true;
    }
  }
  return false;
}


static bool needsBusyCheck(std::string fname)
{ 
  size_t pos = 0;
  if (fname == "WriteMem" ||
      (((pos = fname.find("ReadMem_")) != std::string::npos
         || (pos = fname.find("WriteMem_")) != std::string::npos
         || (pos = fname.find("SendCall_")) != std::string::npos 
         || (pos = fname.find("SendCallFork_")) != std::string::npos 
         || (pos = fname.find("SendReturn_")) != std::string::npos)
       && pos == 0)) {
    return true;
  }
  return false;
} 


static std::string getBusyCheckName(std::string fname)
{ 
  std::string bname;
  size_t pos = 0;
  size_t np = std::string::npos;
  if ((pos = fname.find("ReadMem_")) != np && pos == 0) {
    bname = "ReadMemBusy";
  } else if (((pos = fname.find("WriteMem_")) != np && pos == 0)
             || fname == "WriteMem") {
    bname = "WriteMemBusy";
  } else if ((pos = fname.find("SendCall_")) != np && pos == 0) {
    bname = "SendCallBusy_" + fname.substr(9, np);
  } else if ((pos = fname.find("SendCallFork_")) != np && pos == 0) {
    bname = "SendCallBusy_" + fname.substr(13, np);
  } else if ((pos = fname.find("SendReturn_")) != np && pos == 0) {
     bname = "SendReturnBusy_" + fname.substr(11, np);
  } else {
     bname = "";
  }
  return bname;
}
  
  
class InsertBusyChecks : public AstPrePostProcessing {
private:
  void preOrderVisit(SgNode *S) {
    switch (S->variantT()) {
    case V_SgFunctionDeclaration: {
      currCase = 0;
      currFSM = 0;
      caseToBusyFuncs.clear();
      SgFunctionDeclaration *FD = isSgFunctionDeclaration(S);
      SgFunctionDefinition *fdef = FD->get_definition();
      if (!fdef) {
        break;
      }
      std::set<std::string> allHtFuncs;
      std::vector<SgNode *> l = NodeQuery::querySubTree(S, V_SgFunctionRefExp); 
        foreach (SgNode *ln, l) {
          SgFunctionRefExp *frexp = isSgFunctionRefExp(ln);
          SgFunctionDeclaration *d = frexp->getAssociatedFunctionDeclaration();
          if (d) {
            std::string snm = d->get_name().getString();
            if (needsBusyCheck(snm)) {
              allHtFuncs.insert(snm);
            }
          }
        }

        SgFunctionDeclaration *fdecl = 0;
        foreach (std::string ss, allHtFuncs) {
          std::string bname = getBusyCheckName(ss);
          fdecl = HtDeclMgr::buildHtlFuncDecl_generic(bname, 'b', "v", fdef);
        }
      }
      break;

    case V_SgSwitchStatement:
      if (switchIsFSM(isSgSwitchStatement(S))) {
        currFSM = isSgSwitchStatement(S);
        currCase = 0;
        caseToBusyFuncs.clear();
      }
      break;

    case V_SgCaseOptionStmt: {
        SgCaseOptionStmt *cs = isSgCaseOptionStmt(S); 
        if (cs->get_parent()->get_parent() == currFSM) {
          currCase = cs;
        }
      }
      break;
    default:
      break;
    }
  }


  void postOrderVisit(SgNode *S) {
    switch (S->variantT()) {
    case V_SgSwitchStatement:
      if (switchIsFSM(isSgSwitchStatement(S))) {
        currFSM = 0;
        currCase = 0;
        caseToBusyFuncs.clear();
      }
      break;
    
    case V_SgCaseOptionStmt:
      do {
        SgCaseOptionStmt *cs = isSgCaseOptionStmt(S); 
        if (cs->get_parent()->get_parent() == currFSM
            && caseToBusyFuncs[cs].size() > 0) {
          SgScopeStatement *pscope = SageInterface::getEnclosingProcedure(cs);
          std::vector<SgFunctionCallExp *> busyCalls;
          foreach (std::string bname, caseToBusyFuncs[cs]) {
            SgFunctionCallExp *cbusy = SageBuilder::buildFunctionCallExp(bname,
                SageBuilder::buildBoolType(), SageBuilder::buildExprListExp(),
                pscope);
            busyCalls.push_back(cbusy);
          }
          SgExpression *busyCallExp = 0;
          if (busyCalls.size() == 1) {
            busyCallExp = busyCalls[0];
          } else {
            busyCallExp = SageBuilder::buildOrOp(busyCalls[0], busyCalls[1]);
            for (int i = 2; i < busyCalls.size(); i++) { 
              busyCallExp = SageBuilder::buildOrOp(busyCallExp, busyCalls[i]);
            }
          } 
          SgStatement *retryCallStmt = 
              SageBuilder::buildFunctionCallStmt("HtRetry",
              SageBuilder::buildVoidType(), SageBuilder::buildExprListExp(),
              pscope);
          SgStatement *brkStmt = SageBuilder::buildBreakStmt();
          SgIfStmt *newIfStmt = SageBuilder::buildIfStmt(busyCallExp,
              SageBuilder::buildBasicBlock(retryCallStmt, brkStmt), 0);
          SageInterface::prependStatement(newIfStmt,
              isSgBasicBlock(cs->get_body()));
          currCase = 0;
        }
      } while (0);
      break;

    case V_SgFunctionRefExp: 
      do {
        SgFunctionRefExp *frexp = isSgFunctionRefExp(S);
        SgFunctionDeclaration *decl = frexp->getAssociatedFunctionDeclaration();
        if (decl) {
          std::string snm = decl->get_name();
          if (currCase && needsBusyCheck(snm)) {
            caseToBusyFuncs[currCase].insert(getBusyCheckName(snm));
          }
        }
      } while (0);
      break;

    default:
      break;
    }
  }

  std::map<SgCaseOptionStmt *, std::set<std::string> > caseToBusyFuncs;
  SgCaseOptionStmt *currCase;
  SgSwitchStatement *currFSM;
};


void RewriteAsStateMachine(SgProject *project)
{
  RewriteFSM rv;
  rv.traverseInputFiles(project, preorder, STRICT_INPUT_FILE_TRAVERSAL);

  InsertBusyChecks bv;
  bv.traverseInputFiles(project, STRICT_INPUT_FILE_TRAVERSAL);
}




