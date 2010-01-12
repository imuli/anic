#ifndef _SEMMER_H_
#define _SEMMER_H_

#include "mainDefs.h"
#include "system.h"
#include "constantDefs.h"

#include "lexer.h"
#include "parser.h"

// SymbolTable node kinds

#define KIND_BLOCK 1
#define KIND_STD 2
#define KIND_IMPORT 3
#define KIND_STATIC_DECL 4
#define KIND_THROUGH_DECL 5
#define KIND_PARAM 6

class SymbolTable {
	public:
		// data members
		int kind;
		string id;
		Tree *defSite; // where the symbol is defined in the Tree; NULL for root block and standard nodes
		SymbolTable *parent;
		vector<SymbolTable *> children;
		// allocators/deallocators
		SymbolTable(int kind, string id, Tree *defSite);
		SymbolTable(SymbolTable &st);
		~SymbolTable();
		// deep-copy assignment operator
		SymbolTable &operator=(SymbolTable &st);
		// concatenators
		SymbolTable &operator*=(SymbolTable *st);
};

// Type kind specifiers

#define USR 0

#define STD_NULL 1
#define STD_NODE 2
#define STD_INT 3
#define STD_FLOAT 4
#define STD_BOOL 5
#define STD_CHAR 6
#define STD_STRING 7

// for the following, base will point to the actual operator token

#define STD_PREFIX_OP 8
#define STD_INFIX_OP 9
#define STD_MULTI_OP 10

// Type suffix specifiers

#define SUFFIX_NONE 0
#define SUFFIX_LATCH -1

class Type {
	public:
		// data members
		int kind; // the class of type that this is
		Tree *base; // the Node that defines this type
		int suffix; // positive values indicate stream depth
		Type *next; // the next part of the type's compounding
		// allocators/deallocators
		Type(int kind);
		Type(int kind, Tree *base);
		Type(int kind, Tree *base, int suffix);
		~Type();
};

// main semantic analysis function

int sem(Tree *rootParseme, SymbolTable *&stRoot, bool verboseOutput, int optimizationLevel, bool eventuallyGiveUp);

#endif
