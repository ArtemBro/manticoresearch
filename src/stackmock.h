//
// Copyright (c) 2017-2021, Manticore Software LTD (https://manticoresearch.com)
// Copyright (c) 2001-2016, Andrew Aksyonoff
// Copyright (c) 2008-2016, Sphinx Technologies Inc
// All rights reserved
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License. You should have
// received a copy of the GPL license along with this program; if you
// did not, you can find it at http://www.gnu.org/
//

#ifndef _stackmock_
#define _stackmock_

#include "sphinx.h"

template <typename T>
bool EvalStackForTree ( const CSphVector<T> & dTree, int iStartNode, int iNodeStackSize, int iTreeSizeThresh, int & iStackNeeded, const char * szName, CSphString & sError )
{
	iStackNeeded = -1;
	int64_t iCalculatedStack = sphGetStackUsed() + (int64_t)dTree.GetLength()*iNodeStackSize;
	int64_t iCurStackSize = sphMyStackSize();
	if ( dTree.GetLength()<=iTreeSizeThresh && iCalculatedStack<=iCurStackSize )
		return true;

	CSphVector<std::pair<int,int>> dNodes;
	dNodes.Reserve ( dTree.GetLength()/2 );
	int iMaxHeight = 1;
	dNodes.Add ( { iStartNode, 1 } );
	while ( dNodes.GetLength() )
	{
		auto tParent = dNodes.Pop();
		const auto & tItem = dTree[tParent.first];
		iMaxHeight = Max ( iMaxHeight, tParent.second );
		if ( tItem.m_iLeft>=0 )		dNodes.Add ( { tItem.m_iLeft,  tParent.second+1 } );
		if ( tItem.m_iRight>=0 )	dNodes.Add ( { tItem.m_iRight, tParent.second+1 } );
	}

	iCalculatedStack = sphGetStackUsed() + iMaxHeight*iNodeStackSize;
	if ( iCalculatedStack>g_iMaxCoroStackSize )
	{
		sError.SetSprintf ( "query %s too complex, not enough stack (thread_stack=%dK or higher required)", szName, (int)( ( iCalculatedStack + 1024 - ( iCalculatedStack%1024 ) ) / 1024 ) );
		return false;
	}

	if ( iCurStackSize<=iCalculatedStack )
		iStackNeeded = iCalculatedStack + 32*1024;

	return true;
}

void DetermineNodeItemStackSize();
void DetermineFilterItemStackSize();

#endif // _stackmock_
