#include "semmer.h"

#include "customOperators.h"

// semmer-global variables

int semmerErrorCode;
bool semmerEventuallyGiveUp;

// SymbolTable functions

// allocators/deallocators
SymbolTable::SymbolTable(int kind, string id, Tree *defSite) : kind(kind), id(id), defSite(defSite), parent(NULL) {}

SymbolTable::SymbolTable(SymbolTable &st) {
	*this = st;
}

SymbolTable::~SymbolTable() {
	// delete all of the child nodes
	for (vector<SymbolTable *>::iterator childIter = children.begin(); childIter != children.end(); childIter++) {
		delete *childIter;
	}
}

// deep-copy assignment operator
SymbolTable &SymbolTable::operator=(SymbolTable &st) {
	kind = st.kind;
	id = st.id;
	defSite = st.defSite;
	parent = st.parent;
	for (vector<SymbolTable *>::iterator childIter = st.children.begin(); childIter != st.children.end(); childIter++) {
		// copy the child node
		SymbolTable *child = new SymbolTable(**childIter);
		// fix the child's parent pointer to point up to this node
		child->parent = this;
		// finally, log the child in the copied child into the children list
		children.push_back(child);
	}
	return *this;
}

// concatenators
SymbolTable &SymbolTable::operator*=(SymbolTable *st) {
	// first, check for conflicting bindings
	if (st != NULL && st->id[0] != '_') { // if this is not a special system-level binding
		// per-symbol loop
		for (vector<SymbolTable *>::iterator childIter = children.begin(); childIter != children.end(); childIter++) {
			if ((*childIter)->id == st->id) { // if we've found a conflict
				Token curDefToken;
				if (st->defSite != NULL) { // if there is a definition site for the current symbol
					curDefToken = st->defSite->t;
				} else { // otherwise, it must be a standard definition, so make up the token as if it was
					curDefToken.fileName = STANDARD_LIBRARY_STRING;
					curDefToken.row = 0;
					curDefToken.col = 0;
				}
				Token prevDefToken;
				if ((*childIter)->defSite != NULL) { // if there is a definition site for the previous symbol
					prevDefToken = (*childIter)->defSite->t;
				} else { // otherwise, it must be a standard definition, so make up the token as if it was
					prevDefToken.fileName = STANDARD_LIBRARY_STRING;
					prevDefToken.row = 0;
					prevDefToken.col = 0;
				}
				printSemmerError(curDefToken.fileName,curDefToken.row,curDefToken.col,"redefinition of '"<<st->id<<"'",*this);
				printSemmerError(prevDefToken.fileName,prevDefToken.row,prevDefToken.col,"-- (previous definition was here)",*this);
				delete st;
				return *this;
			} // if there's a conflict
		} // for per-symbol loop
	} // if this is not a special system-level binding

	// binding is now known to be conflict-free, so log it normally
	children.push_back(st);
	if (st != NULL) {
		st->parent = this;
		return *st;
	} else {
		return *this;
	}
}

// Type functions

// allocators/deallocators

Type::Type(int kind) : kind(kind), base(NULL), suffix(SUFFIX_NONE), next(NULL) {}
Type::Type(int kind, Tree *base) : kind(kind), base(base), suffix(SUFFIX_NONE), next(NULL) {}
Type::Type(int kind, Tree *base, int suffix) : kind(kind), base(base), suffix(suffix), next(NULL) {}

Type::~Type() {
	delete next;
}

// Main semantic analysis functions

void catStdNodes(SymbolTable *&stRoot) {
	*stRoot *= new SymbolTable(KIND_STD, "node", NULL);
	*stRoot *= new SymbolTable(KIND_STD, "int", NULL);
	*stRoot *= new SymbolTable(KIND_STD, "float", NULL);
	*stRoot *= new SymbolTable(KIND_STD, "bool", NULL);
	*stRoot *= new SymbolTable(KIND_STD, "char", NULL);
	*stRoot *= new SymbolTable(KIND_STD, "string", NULL);
}

void catStdLib(SymbolTable *&stRoot) {
	// standard root
	SymbolTable *stdLib = new SymbolTable(KIND_STD, STANDARD_LIBRARY_STRING, NULL);

	// system nodes
	// streams
	*stdLib *= new SymbolTable(KIND_STD, "in", NULL);
	*stdLib *= new SymbolTable(KIND_STD, "out", NULL);
	*stdLib *= new SymbolTable(KIND_STD, "err", NULL);
	// control nodes
	*stdLib *= new SymbolTable(KIND_STD, "rand", NULL);
	*stdLib *= new SymbolTable(KIND_STD, "delay", NULL);

	// standard library
	// containers
	*stdLib *= new SymbolTable(KIND_STD, "stack", NULL);
	*stdLib *= new SymbolTable(KIND_STD, "map", NULL);
	// filters
	*stdLib *= new SymbolTable(KIND_STD, "filter", NULL);
	*stdLib *= new SymbolTable(KIND_STD, "sort", NULL);
	// generators
	*stdLib *= new SymbolTable(KIND_STD, "gen", NULL);

	// concatenate the library to the root
	*stRoot *= stdLib;
}

SymbolTable *genDefaultDefs() {
	// generate the root block node
	SymbolTable *stRoot = new SymbolTable(KIND_BLOCK, BLOCK_NODE_STRING, NULL);
	// concatenate in the standard types
	catStdNodes(stRoot);
	// concatenate in the standard library
	catStdLib(stRoot);
	// finally, return the genrated default symtable
	return stRoot;
}

// recursively extracts the appropriate nodes from the given tree and appropriately populates the passed containers
void extractNodes(Tree *parseme, SymbolTable *st, vector<SymbolTable *> &importList, vector<Tree *> &instanceList, vector<Tree *> &netsList, bool netsHandled) {
	// base case
	if (parseme == NULL) {
		return;
	}
	// log the current symbol environment in the parseme
	parseme->env = st;
	// recursive cases
	if (parseme->t.tokenType == TOKEN_Identifier) { // if it's an Identifier
		if (!(parseme->back != NULL && parseme->back->t.tokenType == TOKEN_AT)) { // if it's non-import
			// log this identifier use case
			instanceList.push_back(parseme);
			// *don't* recurse any deeper in this Identifier
		} else { // else if it's an import Identifier
			// recurse on the right only; i.e. don't log import subidentifiers as use cases
			extractNodes(parseme->next, st, importList, instanceList, netsList, netsHandled); // right
		}
	} else if (parseme->t.tokenType == TOKEN_NonEmptyTerms && !netsHandled) { // if it's a term stream node
		// log the stream occurence
		netsList.push_back(parseme);
		// recurse down only; NonEmptyTerms never has any right siblings
		extractNodes(parseme->child, st, importList, instanceList, netsList, true); // right
	} else if (parseme->t.tokenType == TOKEN_Block) { // if it's a block node
		// allocate the new definition node
		SymbolTable *blockDef = new SymbolTable(KIND_BLOCK, BLOCK_NODE_STRING, parseme);
		// if there is a header attatched to this block, inject its definitions into the block node
		if (parseme->back != NULL && parseme->back->t.tokenType == TOKEN_NodeHeader) {
			Tree *nh = parseme->back; // NodeHeader
			if (nh->child->next->child != NULL) { // if there is a parameter list to process
				Tree *param = nh->child->next->child->child; // Param
				for (;;) { // per-param loop
					// allocate the new parameter definition node
					SymbolTable *paramDef = new SymbolTable(KIND_PARAM, param->child->next->t.s, param);
					// ... and link it into the block node
					*blockDef *= paramDef;
					// advance
					if (param->next != NULL) {
						param = param->next->next->child; // Param
					} else {
						break;
					}
				} // per-param loop
			}
		} // if there is a header attatched to this block
		// finally, link the block node into the main trunk
		*st *= blockDef;
		// recurse
		extractNodes(parseme->child, blockDef, importList, instanceList, netsList, netsHandled); // child of Block
	} else if (parseme->t.tokenType == TOKEN_Declaration) { // if it's a declaration node
		Token t = parseme->child->next->t;
		if (t.tokenType == TOKEN_EQUALS) { // standard static declaration
			// allocate the new definition node
			SymbolTable *newDef = new SymbolTable(KIND_STATIC_DECL, parseme->child->t.s, parseme);
			// ... and link it in
			*st *= newDef;
			// recurse
			extractNodes(parseme->child, newDef, importList, instanceList, netsList, netsHandled); // child of Declaration
		} else if (t.tokenType == TOKEN_ERARROW) { // flow-through declaration
			// allocate the new definition node
			SymbolTable *newDef = new SymbolTable(KIND_THROUGH_DECL, parseme->child->t.s, parseme);
			// ... and link it in
			*st *= newDef;
			// recurse
			extractNodes(parseme->child, newDef, importList, instanceList, netsList, netsHandled); // child of Declaration
		} else if (t.tokenType == TOKEN_Identifier) { // import declaration
			// allocate the new definition node
			SymbolTable *newDef = new SymbolTable(KIND_IMPORT, IMPORT_DECL_STRING, parseme);
			// ... and link it in
			*st *= newDef;
			// also, since it's an import declaration, log it to the import list
			importList.push_back(newDef);
			// recurse
			extractNodes(parseme->child, newDef, importList, instanceList, netsList, netsHandled); // child of Declaration
		}
	} else { // else if it's not a declaration node
		// recurse normally
		extractNodes(parseme->child, st, importList, instanceList, netsList, netsHandled); // down
		extractNodes(parseme->next, st, importList, instanceList, netsList, netsHandled); // right
	}
}

// binds qualified identifiers in the given symtable environment; returns the tail of the binding
// returns NULL if no binding can be found
SymbolTable *bindId(string qi, SymbolTable *env) {
	// base case
	if (env == NULL) {
		return NULL;
	}
	// recall identifier case
	if (qi[0] == '.' && qi[1] == '.') {
// LOL
	}
	// recursive case
	string tip = idTip(qi);
	// scan the current environment's children for a latch point
	for (vector<SymbolTable *>::iterator latchIter = env->children.begin(); latchIter != env->children.end(); latchIter++) {
		if ((*latchIter)->id == tip) { // if we've found a latch point

			// verify that the latching holds for the rest of the identifier
			SymbolTable *stCur = *latchIter;
			vector<string> choppedQI = qiChop(qi);
			unsigned int i = 1; // start at 1, since we've aleady matched the tip (index 0)
			while (i < choppedQI.size()) {

				// find a static match in the current st node's children
				SymbolTable *match = NULL;
				for (vector<SymbolTable *>::iterator stcIter = stCur->children.begin(); stcIter != stCur->children.end(); stcIter++) {

					if ((*stcIter)->id == choppedQI[i]) { // if the identifiers are the same, we have a match
						match = *stcIter;
						goto matchOK;

					// as a special case, look one block level deeper, since nested defs must be block-delimited
					} else if (stCur->kind != KIND_BLOCK && (*stcIter)->kind == KIND_BLOCK) {
						for (vector<SymbolTable *>::iterator blockIter = (*stcIter)->children.begin(); blockIter != (*stcIter)->children.end(); blockIter++) {
							if ((*blockIter)->id[0] != '_' && (*blockIter)->id == choppedQI[i]) { // if the identifiers are the same, we have a match
								match = *blockIter;
								goto matchOK;
							}
						}
					}

				} matchOK: ; // match verification loop

				// if we don't have a static match, look for a dynamic match stemming from stCur
				if (match == NULL) {
					if (stCur->kind == KIND_STATIC_DECL) {
// LOL
					} else if (stCur->kind == KIND_THROUGH_DECL) {
// LOL
					} else if (stCur->kind == KIND_PARAM) {
// LOL
					}
				}

				if (match != NULL) { // if we ultimately do have a match, advance
					// advance to the matched st node
					stCur = match;
					// advance to the next token in the qi
					i++;
				} else { // else if we don't have a match, fail
					break;
				}

			}
			// if we've verified the entire qi, return the tail of the latch point
			if (i == choppedQI.size()) {
				return stCur;
			}
			// no need to look thrugh the rest of the children; we've already found the correctly named one on this level
			break;
		}
	} // per-latch point loop

	// otherwise, recursively try to find a binding starting one level higher
	// but first, scan up to the next block level, since jumping to the enclosing identifier is a waste of time
	SymbolTable *recurseSt = env->parent;
	while (recurseSt != NULL && recurseSt->kind != KIND_BLOCK) {
		recurseSt = recurseSt->parent;
	}
	return bindId(qi, recurseSt);
}

void subImportDecls(vector<SymbolTable *> &importList) {
	bool stdExplicitlyImported = false;
	// per-import loop
	for (vector<SymbolTable *>::iterator importIter = importList.begin(); importIter != importList.end(); importIter++) {
		// extract the import path out of the iterator
		string importPath = id2String((*importIter)->defSite->child->next);
		// standard import special-casing
		if (importPath == "std") { // if it's the standard import
			if (!stdExplicitlyImported) { // if it's the first standard import, flag it as handled and let it slide
				(*importIter)->id = STANDARD_IMPORT_DECL_STRING;
				stdExplicitlyImported = true;
				continue;
			}
		}
		// try to find a binding for this import
		SymbolTable *binding = bindId(importPath, *importIter);
		if (binding != NULL) { // if we found a binding
			// check to make sure that this import doesn't cause a binding conflict
			string importPathTip = binding->id; // must exist if binding succeeed
			// per-parent's children loop (parent must exist, since the root is a block st node)
			vector<SymbolTable *>::iterator childIter = (*importIter)->parent->children.begin();
			while (childIter != (*importIter)->parent->children.end()) {
				if ((*childIter)->id[0] != '_' && (*childIter)->id == importPathTip) { // if there's a conflict
					Token curDefToken = (*importIter)->defSite->child->next->child->t; // child of Identifier
					Token prevDefToken;
					if ((*childIter)->defSite != NULL) { // if there is a definition site for the previous symbol
						prevDefToken = (*childIter)->defSite->t;
					} else { // otherwise, it must be a standard definition, so make up the token as if it was
						prevDefToken.fileName = STANDARD_LIBRARY_STRING;
						prevDefToken.row = 0;
						prevDefToken.col = 0;
					}
					printSemmerError(curDefToken.fileName,curDefToken.row,curDefToken.col,"name conflict in importing '"<<importPathTip<<"'",);
					printSemmerError(prevDefToken.fileName,prevDefToken.row,prevDefToken.col,"-- (conflicting definition was here)",);
					goto nextImport;
				}
				// advance
				childIter++;
			}
			// there was no conflict, so just deep-copy the binding in place of the import placeholder node
			**importIter = *binding;
		} else { // else if no binding could be found
			Token t = (*importIter)->defSite->t;
			printSemmerError(t.fileName,t.row,t.col,"cannot resolve import '"<<importPath<<"'",);
		}
		nextImport: ;
	} // per-import loop
}

void bindInstances(vector<Tree *> &instanceList) {
	// per-instance loop
	for (vector<Tree *>::iterator instanceIter = instanceList.begin(); instanceIter != instanceList.end(); instanceIter++) {
		Tree *qi = *instanceIter;
		string qiString = id2String(qi);
		SymbolTable *binding = bindId(qiString, qi->env);
		if (binding != NULL) { // if we found a binding for this identifier, latch it
			qi->env = binding;
		} else { // else if we couldn't find a binding
			Token curQIToken = qi->child->t;
			printSemmerError(curQIToken.fileName,curQIToken.row,curQIToken.col,"cannot resolve '"<<qiString<<"'",);
		}
	}
}

string type2String(Type *t) {
	return ""; // stub
}

Type *getExpType(Type *inType, Tree *exp) {
	return (Type *)0x4; // stub
}

Type *getTermType(Type *inType, Tree *term) {
	Tree *tc2 = term->child->child;
	if (tc2->t.tokenType == TOKEN_SimpleCondTerm) {

	} else if (tc2->t.tokenType == TOKEN_OpenCondTerm) {

	} else if (tc2->t.tokenType == TOKEN_SimpleTerm) {
		Tree *tc3 = tc2->child;
		if (tc3->t.tokenType == TOKEN_DynamicTerm) {
			Tree *tc4 = tc3->child;
			if (tc4->t.tokenType == TOKEN_StaticTerm) {
				Tree *tc5 = tc4->child;
				if (tc5->t.tokenType == TOKEN_TypedStaticTerm) {
					Tree *tc6 = tc5->child;
					if (tc6->t.tokenType == TOKEN_Node) {
						Tree *tc7 = tc6->child;
						if (tc7->t.tokenType == TOKEN_Identifier) {

						} else if (tc7->t.tokenType == TOKEN_NodeInstantiation) {

						} else if (tc7->t.tokenType == TOKEN_TypedNodeLiteral) {

						} else if (tc7->t.tokenType == TOKEN_PrimOpNode) {
							Tree *tc8 = tc7->child; // type of operator
							Tree *tc9 = tc8->child; // the operator token itself
							if (tc8->t.tokenType == TOKEN_PrefixOp) {
								return new Type(STD_PREFIX_OP, tc9);
							} else if (tc8->t.tokenType == TOKEN_InfixOp) {
								return new Type(STD_INFIX_OP, tc9);
							} else if (tc8->t.tokenType == TOKEN_MultiOp) {
								return new Type(STD_MULTI_OP, tc9);
							}
						} else if (tc7->t.tokenType == TOKEN_PrimLiteral) {
							Tree *tc8 = tc7->child;
							if (tc8->t.tokenType == TOKEN_INUM) {
								return new Type(STD_INT);
							} else if (tc8->t.tokenType == TOKEN_FNUM) {
								return new Type(STD_FLOAT);
							} else if (tc8->t.tokenType == TOKEN_CQUOTE) {
								return new Type(STD_CHAR);
							} else if (tc8->t.tokenType == TOKEN_SQUOTE) {
								return new Type(STD_STRING);
							}
						}
					} else if (tc6->t.tokenType == TOKEN_LBRACKET) { // it's an expression
						Type *expType = getExpType(inType, tc6->next);
						tc6->type = expType;
						return expType;
					}
				} else if (tc5->t.tokenType == TOKEN_Delatch) {

				} else if (tc5->t.tokenType == TOKEN_Block) {

				}
			} else if (tc4->t.tokenType == TOKEN_Compound) {

			} else if (tc4->t.tokenType == TOKEN_Link) {

			} else if (tc4->t.tokenType == TOKEN_Send) {

			}
		} else if (tc3->t.tokenType == TOKEN_SwitchTerm) {

		}
	} else if (tc2->t.tokenType == TOKEN_ClosedCondTerm) {

	}
	return (Type *)0x4;
}

void traceTypes(vector<Tree *> &netsList) {
	// simply iterate through the list of NonEmptyTerms and trace the types for each one, starting with nullity
	for (unsigned int i=0; i < netsList.size(); i++) {
		// temporaily allocate the null type
		Type *nullType = new Type(STD_NULL);
		// scan the pipe left to right
		Tree *curTerm = netsList[i]->child;
		Type *inType = nullType;
		while (curTerm != NULL) {
			Type *outType = getTermType(inType, curTerm);
			if (outType != NULL) { // if we found a proper typing for this term, log it
				curTerm->type = outType;
			} else { // otherwise, if we were unable to assign a type to the term, flag an error
				Token curToken = curTerm->t;
				printSemmerError(curToken.fileName,curToken.row,curToken.col,"cannot resolve output type for this term",);
				printSemmerError(curToken.fileName,curToken.row,curToken.col,"-- (input type was "<<type2String(inType)<<")",);
				// skip typing this pipe and move on to the next one
				break;
			}
			// advance
			curTerm = curTerm->next->child; // Term
		}
	}
}

// main semming function; makes no assumptions about stRoot's value; it's just a return parameter
int sem(Tree *rootParseme, SymbolTable *&stRoot, bool verboseOutput, int optimizationLevel, bool eventuallyGiveUp) {

	// initialize error code
	semmerErrorCode = 0;
	semmerEventuallyGiveUp = eventuallyGiveUp;

	VERBOSE( printNotice("Binding identifiers..."); )

	// initialize the symbol table root with the default definitions
	stRoot = genDefaultDefs();

	// populate the symbol table with definitions from the user parseme, and log the used imports/id instances
	vector<SymbolTable *> importList; // import Declaration nodes
	vector<Tree *> instanceList; // top-level non-import Identifier nodes
	vector<Tree *> netsList; // list of top-level Term nodes
	extractNodes(rootParseme, stRoot, importList, instanceList, netsList, false);

	// substitute import declarations
	subImportDecls(importList);

	VERBOSE( cout << stRoot; )

	// bind identifier use sites to their definitions, checking for errors
	bindInstances(instanceList);

	VERBOSE( printNotice("Tracing type flow..."); )

	// assign types to all node streams
	traceTypes(netsList);

	// finally, return to the caller
	return semmerErrorCode ? 1 : 0;
}
