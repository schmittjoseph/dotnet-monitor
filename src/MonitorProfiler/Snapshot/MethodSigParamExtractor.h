// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include "../Utilities/sigparse.h"
#include <vector>

class MethodSigParamExtractor : public SigParser
{
private:
        sig_count m_paramCount;
		BOOL m_hasThis;
		BOOL m_inParam;
		BOOL m_inRet;
		INT32 m_sigLevel;
		std::vector<sig_elem_type> m_ArgTypes;

		BOOL ShouldRecordParamType()
		{
			// 1 - method
			// 2 - param
			// 3 - type
			return (m_sigLevel == 3);
		}

public:
	    MethodSigParamExtractor()
		{ 
			m_paramCount = 0;
			m_sigLevel = 0;
			m_hasThis = FALSE;
			m_inParam = FALSE;
			m_inRet = FALSE;
		}

        sig_count GetParamCount() { return m_paramCount; }
		BOOL GetHasThis() { return m_hasThis; }
		std::vector<sig_elem_type> GetParamTypes()
		{
			return m_ArgTypes;
		}

    
protected:

 	// a method with given elem_type
	virtual void NotifyBeginMethod(sig_elem_type elem_type)
	{
		m_sigLevel++;
	}

	virtual void NotifyEndMethod()
	{
		m_sigLevel--;
	}

	virtual void NotifyHasThis()
	{
		// 1 - method
		if (m_sigLevel == 1) 
		{
			m_hasThis = TRUE;
		}
	}

 	// total parameters for the method
	virtual void NotifyParamCount(sig_count count)
	{
        m_paramCount = count;
	}

 	// starting a return type
	virtual void NotifyBeginRetType()
	{
		m_sigLevel++;
		m_inRet = TRUE;
	}

	virtual void NotifyEndRetType()
	{
		m_sigLevel--;
		m_inRet = FALSE;
	}

 	// starting a parameter
	virtual void NotifyBeginParam()
	{
		m_sigLevel++;
		m_inParam = TRUE;
	}

	virtual void NotifyEndParam()
	{
		m_sigLevel--;
		m_inParam = FALSE;
	}

 	// sentinel indication the location of the "..." in the method signature
	virtual void NotifySentinel()
	{
	}

 	// number of generic parameters in this method signature (if any)
	virtual void NotifyGenericParamCount(sig_count count)
	{
	}

	//----------------------------------------------------

 	// a field with given elem_type
	virtual void NotifyBeginField(sig_elem_type elem_type)
	{
		m_sigLevel++;
	}

	virtual void NotifyEndField()
	{
		m_sigLevel--;
	}

	//----------------------------------------------------

	// a block of locals with given elem_type (always just LOCAL_SIG for now)
	virtual void NotifyBeginLocals(sig_elem_type elem_type)
	{
		m_sigLevel++;
	}

	virtual void NotifyEndLocals()
	{
		m_sigLevel--;
	}


 	// count of locals with a block
	virtual void NotifyLocalsCount(sig_count count)
	{
	}

 	// starting a new local within a local block
	virtual void NotifyBeginLocal()
	{
		m_sigLevel++;
	}

	virtual void NotifyEndLocal()
	{
		m_sigLevel--;
	}


 	// the only constraint available to locals at the moment is ELEMENT_TYPE_PINNED
	virtual void NotifyConstraint(sig_elem_type elem_type)
	{
	}


	//----------------------------------------------------

	// a property with given element type
	virtual void NotifyBeginProperty(sig_elem_type elem_type)
	{
		m_sigLevel++;
	}

	virtual void NotifyEndProperty()
	{
		m_sigLevel--;
	}


	//----------------------------------------------------

	// starting array shape information for array types
	virtual void NotifyBeginArrayShape()
	{
		m_sigLevel++;
	}

	virtual void NotifyEndArrayShape()
	{
		m_sigLevel--;
	}


 	// array rank (total number of dimensions)
	virtual void NotifyRank(sig_count count)
	{
	}

 	// number of dimensions with specified sizes followed by the size of each
	virtual void NotifyNumSizes(sig_count count)
	{
	}

	virtual void NotifySize(sig_count count)
	{
	}

	// BUG BUG lower bounds can be negative, how can this be encoded?
	// number of dimensions with specified lower bounds followed by lower bound of each
	virtual void NotifyNumLoBounds(sig_count count)
	{
	}

	virtual void NotifyLoBound(sig_count count)
	{
	}

	//----------------------------------------------------


	// starting a normal type (occurs in many contexts such as param, field, local, etc)
	virtual void NotifyBeginType()
	{
		// method = 1
		// param = 2
		// 
		m_sigLevel++;
	}

	virtual void NotifyEndType()
	{
		m_sigLevel--;
	}

	virtual void NotifyTypedByref()
	{
	}

	// the type has the 'byref' modifier on it -- this normally proceeds the type definition in the context
	// the type is used, so for instance a parameter might have the byref modifier on it
	// so this happens before the BeginType in that context
	virtual void NotifyByref()
	{
	}

 	// the type is "VOID" (this has limited uses, function returns and void pointer)
	virtual void NotifyVoid()
	{
		if (!ShouldRecordParamType()) {
			return;
		}

		m_ArgTypes.push_back(ELEMENT_TYPE_VOID);
	}

 	// the type has the indicated custom modifiers (which can be optional or required)
	virtual void NotifyCustomMod(sig_elem_type cmod, sig_index_type indexType, sig_index index)
	{
	}

	// the type is a simple type, the elem_type defines it fully
	virtual void NotifyTypeSimple(sig_elem_type elem_type)
	{
		if (!ShouldRecordParamType()) {
			return;
		}

		m_ArgTypes.push_back(elem_type);
	}

	// the type is specified by the given index of the given index type (normally a type index in the type metadata)
	// this callback is normally qualified by other ones such as NotifyTypeClass or NotifyTypeValueType
	virtual void NotifyTypeDefOrRef(sig_index_type indexType, int index)
	{
	}

	// the type is an instance of a generic
	// elem_type indicates value_type or class
	// indexType and index indicate the metadata for the type in question
	// number indicates the number of type specifications for the generic types that will follow
	virtual void NotifyTypeGenericInst(sig_elem_type elem_type, sig_index_type indexType, sig_index index, sig_mem_number number)
	{
	}

	// the type is the type of the nth generic type parameter for the class
	virtual void NotifyTypeGenericTypeVariable(sig_mem_number number)
	{
	}

	// the type is the type of the nth generic type parameter for the member
	virtual void NotifyTypeGenericMemberVariable(sig_mem_number number)
	{
	}

	// the type will be a value type
	virtual void NotifyTypeValueType()
	{
		if (!ShouldRecordParamType()) {
			return;
		}

		m_ArgTypes.push_back(ELEMENT_TYPE_VALUETYPE);
	}

	// the type will be a class
	virtual void NotifyTypeClass()
	{
		if (!ShouldRecordParamType()) {
			return;
		}

		m_ArgTypes.push_back(ELEMENT_TYPE_CLASS);
		// Ignore next NotifyTypeDefOrRef
	}

 	// the type is a pointer to a type (nested type notifications follow)
	virtual void NotifyTypePointer()
	{
		if (!ShouldRecordParamType()) {
			return;
		}

		m_ArgTypes.push_back(ELEMENT_TYPE_PTR);
	}

 	// the type is a function pointer, followed by the type of the function
	virtual void NotifyTypeFunctionPointer()
	{
		if (!ShouldRecordParamType()) {
			return;
		}

		m_ArgTypes.push_back(ELEMENT_TYPE_FNPTR);
	}

 	// the type is an array, this is followed by the array shape, see above, as well as modifiers and element type
	virtual void NotifyTypeArray()
	{
		if (!ShouldRecordParamType()) {
			return;
		}

		m_ArgTypes.push_back(ELEMENT_TYPE_ARRAY);
		// Ignore next NotifyTypeDefOrRef
	}

 	// the type is a simple zero-based array, this has no shape but does have custom modifiers and element type
	virtual void NotifyTypeSzArray()
	{
		if (!ShouldRecordParamType()) {
			return;
		}

		m_ArgTypes.push_back(ELEMENT_TYPE_SZARRAY);
	}

};