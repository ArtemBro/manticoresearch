//
// $Id$
//

//
// Copyright (c) 2017-2019, Manticore Software LTD (http://manticoresearch.com)
// All rights reserved
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License. You should have
// received a copy of the GPL license along with this program; if you
// did not, you can find it at http://www.gnu.org/
//

#include "docstore.h"

#include "sphinxint.h"
#include "attribute.h"
#include "lz4/lz4.h"
#include "lz4/lz4hc.h"

enum BlockFlags_e : BYTE
{
	BLOCK_FLAG_COMPRESSED		= 1 << 0,
	BLOCK_FLAG_FIELD_REORDER	= 1 << 1
};

enum BlockType_e : BYTE
{
	BLOCK_TYPE_SMALL,
	BLOCK_TYPE_BIG
};

enum DocFlags_e : BYTE
{
	DOC_FLAG_ALL_EMPTY		= 1 << 0,
	DOC_FLAG_EMPTY_BITMASK	= 1 << 1
};

enum FieldFlags_e : BYTE
{
	FIELD_FLAG_COMPRESSED	= 1 << 0,
	FIELD_FLAG_EMPTY		= 1 << 1
};

static const int STORAGE_VERSION = 1;

//////////////////////////////////////////////////////////////////////////

static BYTE Compression2Byte ( Compression_e eComp )
{
	switch (eComp)
	{
	case Compression_e::NONE:	return 0;
	case Compression_e::LZ4:	return 1;
	case Compression_e::LZ4HC:	return 2;
	default:
		assert ( 0 && "Unknown compression type" );
		return 0;
	}
}


static Compression_e Byte2Compression ( BYTE uComp )
{
	switch (uComp)
	{
	case 0:		return Compression_e::NONE;
	case 1:		return Compression_e::LZ4;
	case 2:		return Compression_e::LZ4HC;
	default:
		assert ( 0 && "Unknown compression type" );
		return Compression_e::NONE;
	}
}


static void PackData ( CSphVector<BYTE> & dDst, const BYTE * pData, DWORD uSize, bool bText, bool bPack )
{
	if ( bPack )
	{
		const DWORD GAP = 8;
		dDst.Resize ( uSize+GAP );
		BYTE * pEnd = sphPackPtrAttr ( dDst.Begin(), pData, uSize );
		dDst.Resize ( pEnd-dDst.Begin() );
	}
	else
	{	
		dDst.Reserve(uSize+1);
		dDst.Resize(uSize);
		memcpy ( dDst.Begin(), pData, uSize );
		if ( bText )
			dDst.Add('\0');
	}
}


//////////////////////////////////////////////////////////////////////////
class Compressor_i
{
public:
	virtual			~Compressor_i(){}

	virtual bool	Compress ( const VecTraits_T<BYTE> & dUncompressed, CSphVector<BYTE> & dCompressed ) const = 0;
	virtual bool	Decompress ( const VecTraits_T<BYTE> & dCompressed, VecTraits_T<BYTE> & dDecompressed ) const = 0;
};


class Compressor_None_c : public Compressor_i
{
public:
	bool			Compress ( const VecTraits_T<BYTE> & dUncompressed, CSphVector<BYTE> & dCompressed ) const final { return false; }
	bool			Decompress ( const VecTraits_T<BYTE> & dCompressed, VecTraits_T<BYTE> & dDecompressed ) const  final { return true; }
};


class Compressor_LZ4_c : public Compressor_i
{
public:
	bool			Compress ( const VecTraits_T<BYTE> & dUncompressed, CSphVector<BYTE> & dCompressed ) const override;
	bool			Decompress ( const VecTraits_T<BYTE> & dCompressed, VecTraits_T<BYTE> & dDecompressed ) const final;

protected:
	virtual int		DoCompression ( const VecTraits_T<BYTE> & dUncompressed, CSphVector<BYTE> & dCompressed ) const;
};


class Compressor_LZ4HC_c : public Compressor_LZ4_c
{
public:
					Compressor_LZ4HC_c ( int iCompressionLevel );

protected:
	int				DoCompression ( const VecTraits_T<BYTE> & dUncompressed, CSphVector<BYTE> & dCompressed ) const final;

private:
	int				m_iCompressionLevel = DEFAULT_COMPRESSION_LEVEL;
};


bool Compressor_LZ4_c::Compress ( const VecTraits_T<BYTE> & dUncompressed, CSphVector<BYTE> & dCompressed ) const
{
	const int MIN_COMPRESSIBLE_SIZE = 64;
	if ( dUncompressed.GetLength() < MIN_COMPRESSIBLE_SIZE )
		return false;

	dCompressed.Resize ( int ( dUncompressed.GetLength()*1.5f ) );
	int iCompressedSize = DoCompression ( dUncompressed, dCompressed );

	const float WORST_COMPRESSION_RATIO = 0.95f;
	if ( iCompressedSize<0 || float(iCompressedSize)/dUncompressed.GetLength() > WORST_COMPRESSION_RATIO )
		return false;

	dCompressed.Resize(iCompressedSize);
	return true;
}


bool Compressor_LZ4_c::Decompress ( const VecTraits_T<BYTE> & dCompressed, VecTraits_T<BYTE> & dDecompressed ) const
{
	int iRes = LZ4_decompress_safe ( (const char *)dCompressed.Begin(), (char *)dDecompressed.Begin(), dCompressed.GetLength(), dDecompressed.GetLength() );
	return iRes==dDecompressed.GetLength();
}


int	Compressor_LZ4_c::DoCompression ( const VecTraits_T<BYTE> & dUncompressed, CSphVector<BYTE> & dCompressed ) const
{
	return LZ4_compress_default ( (const char *)dUncompressed.Begin(), (char *)dCompressed.Begin(), dUncompressed.GetLength(), dCompressed.GetLength() );
}


Compressor_LZ4HC_c::Compressor_LZ4HC_c ( int iCompressionLevel )
	: m_iCompressionLevel ( iCompressionLevel )
{}


int	Compressor_LZ4HC_c::DoCompression ( const VecTraits_T<BYTE> & dUncompressed, CSphVector<BYTE> & dCompressed ) const
{
	return LZ4_compress_HC ( (const char *)dUncompressed.Begin(), (char *)dCompressed.Begin(), dUncompressed.GetLength(), dCompressed.GetLength(), m_iCompressionLevel );
}


Compressor_i * CreateCompressor ( Compression_e eComp, int iCompressionLevel )
{
	switch (  eComp )
	{
		case Compression_e::LZ4:	return new Compressor_LZ4_c;
		case Compression_e::LZ4HC:	return new Compressor_LZ4HC_c ( iCompressionLevel );
		default:					return new Compressor_None_c;
	}
}

//////////////////////////////////////////////////////////////////////////
class DocstoreFields_c : public DocstoreFields_i
{
public:
	struct Field_t
	{
		CSphString			m_sName;
		DocstoreDataType_e	m_eType;
	};


	int				AddField ( const CSphString & sName, DocstoreDataType_e eType ) final;
	int				GetFieldId ( const CSphString & sName, DocstoreDataType_e eType ) const final;

	int				GetNumFields() const { return m_dFields.GetLength(); }
	const Field_t &	GetField ( int iField ) const { return m_dFields[iField]; }
	void			Load ( CSphReader & tReader );
	void			Save ( CSphWriter & tWriter );

private:
	CSphVector<Field_t>			m_dFields;
	SmallStringHash_T<int>		m_hFields;
};


int DocstoreFields_c::AddField ( const CSphString & sName, DocstoreDataType_e eType )
{
	int iField = m_dFields.GetLength();
	m_dFields.Add ( {sName, eType} );
	
	CSphString sCompound;
	sCompound.SetSprintf ( "%d%s", eType, sName.cstr() );
	m_hFields.Add ( iField, sCompound );

	return iField;
}


int DocstoreFields_c::GetFieldId ( const CSphString & sName, DocstoreDataType_e eType ) const
{
	CSphString sCompound;
	sCompound.SetSprintf ( "%d%s", eType, sName.cstr() );
	return m_hFields[sCompound];
}


void DocstoreFields_c::Load ( CSphReader & tReader )
{
	assert ( !GetNumFields() );

	DWORD uNumFields = tReader.GetDword();
	for ( int i = 0; i < (int)uNumFields; i++ )
	{	
		DocstoreDataType_e eType = (DocstoreDataType_e)tReader.GetByte();
		CSphString sName = tReader.GetString();
		AddField ( sName, eType );
	}
}


void DocstoreFields_c::Save ( CSphWriter & tWriter )
{
	tWriter.PutDword ( GetNumFields() );
	for ( int i = 0; i < GetNumFields(); i++ )
	{
		tWriter.PutByte ( GetField(i).m_eType );
		tWriter.PutString ( GetField(i).m_sName );
	}
}

//////////////////////////////////////////////////////////////////////////

class BlockCache_c
{
public:
	struct BlockData_t
	{
		BYTE	m_uFlags = 0;
		DWORD	m_uNumDocs = 0;
		BYTE *	m_pData = nullptr;
		DWORD	m_uSize = 0;
	};

							BlockCache_c ( int64_t iCacheSize );
							~BlockCache_c();

	bool					Find ( DWORD uUID, SphOffset_t tOffset, BlockData_t & tData );
	bool					Add ( DWORD uUID, SphOffset_t tOffset, BlockData_t & tData );
	void					Release ( DWORD uUID, SphOffset_t tOffset );
	void					DeleteAll ( DWORD uUID );

	static void				Init ( int iCacheSize );
	static void				Done();
	static BlockCache_c *	Get();

private:
	struct HashKey_t
	{
		DWORD		m_uUID;
		SphOffset_t	m_tOffset;

		bool operator == ( const HashKey_t & tKey ) const;
		static DWORD GetHash ( const HashKey_t & tKey );
	};

	struct LinkedBlock_t : BlockData_t
	{
		CSphAtomic		m_iRefcount{0};
		LinkedBlock_t *	m_pPrev = nullptr;
		LinkedBlock_t *	m_pNext = nullptr;
		HashKey_t		m_tKey;
	};

	static BlockCache_c *	m_pBlockCache;

	LinkedBlock_t *			m_pHead = nullptr;
	LinkedBlock_t *			m_pTail = nullptr;
	int64_t					m_iCacheSize = 0;
	int64_t					m_iMemUsed = 0;
	CSphMutex				m_tLock;
	OpenHash_T<LinkedBlock_t *, HashKey_t, HashKey_t> m_tHash;

	void					MoveToHead ( LinkedBlock_t * pBlock );
	void					Add ( LinkedBlock_t * pBlock );
	void					Delete ( LinkedBlock_t * pBlock );
	void					SweepUnused ( DWORD uSpaceNeeded );
	bool					HaveSpaceFor ( DWORD uSpaceNeeded ) const;
};

BlockCache_c * BlockCache_c::m_pBlockCache = nullptr;


bool BlockCache_c::HashKey_t::operator == ( const HashKey_t & tKey ) const
{
	return m_uUID==tKey.m_uUID && m_tOffset==tKey.m_tOffset;
}


DWORD BlockCache_c::HashKey_t::GetHash ( const HashKey_t & tKey )
{
	DWORD uCRC32 = sphCRC32 ( &tKey.m_uUID, sizeof(tKey.m_uUID) );
	return sphCRC32 ( &tKey.m_tOffset, sizeof(tKey.m_tOffset), uCRC32 );
}


BlockCache_c::BlockCache_c ( int64_t iCacheSize )
	: m_iCacheSize ( iCacheSize )
	, m_tHash ( 1024 )
{}


BlockCache_c::~BlockCache_c()
{
	LinkedBlock_t * pBlock = m_pHead;
	while ( pBlock )
	{
		LinkedBlock_t * pToDelete = pBlock;
		pBlock = pBlock->m_pNext;
		Delete(pToDelete);
	}
}


void BlockCache_c::MoveToHead ( LinkedBlock_t * pBlock )
{
	assert ( pBlock );

	if ( pBlock==m_pHead )
		return;

	ScopedMutex_t tLock(m_tLock);

	if ( m_pTail==pBlock )
		m_pTail = pBlock->m_pPrev;

	if ( pBlock->m_pPrev )
		pBlock->m_pPrev->m_pNext = pBlock->m_pNext;

	if ( pBlock->m_pNext )
		pBlock->m_pNext->m_pPrev = pBlock->m_pPrev;

	pBlock->m_pPrev = nullptr;
	pBlock->m_pNext = m_pHead;
	m_pHead->m_pPrev = pBlock;
	m_pHead = pBlock;
}


void BlockCache_c::Add ( LinkedBlock_t * pBlock )
{
	ScopedMutex_t tLock(m_tLock);

	pBlock->m_pNext = m_pHead;
	if ( m_pHead )
		m_pHead->m_pPrev = pBlock;

	if ( !m_pTail )
		m_pTail = pBlock;

	m_pHead = pBlock;

	Verify ( m_tHash.Add ( pBlock->m_tKey, pBlock ) );

	m_iMemUsed += pBlock->m_uSize + sizeof(LinkedBlock_t);
}


void BlockCache_c::Delete ( LinkedBlock_t * pBlock )
{
	ScopedMutex_t tLock(m_tLock);

	Verify ( m_tHash.Delete ( pBlock->m_tKey ) );

	if ( m_pHead == pBlock )
		m_pHead = pBlock->m_pNext;

	if ( m_pTail==pBlock )
		m_pTail = pBlock->m_pPrev;

	if ( pBlock->m_pPrev )
		pBlock->m_pPrev->m_pNext = pBlock->m_pNext;

	if ( pBlock->m_pNext )
		pBlock->m_pNext->m_pPrev = pBlock->m_pPrev;

	m_iMemUsed -= pBlock->m_uSize + sizeof(LinkedBlock_t);
	assert ( m_iMemUsed>=0 );

	SafeDeleteArray ( pBlock->m_pData );
	SafeDelete(pBlock);
}


bool BlockCache_c::Find ( DWORD uUID, SphOffset_t tOffset, BlockData_t & tData )
{
	LinkedBlock_t ** ppBlock = m_tHash.Find ( { uUID, tOffset } );
	if ( !ppBlock )
		return false;

	MoveToHead ( *ppBlock );
	
	(*ppBlock)->m_iRefcount++;
	tData = *(BlockData_t*)(*ppBlock);
	return true;
}


void BlockCache_c::Release ( DWORD uUID, SphOffset_t tOffset )
{
	LinkedBlock_t ** ppBlock = m_tHash.Find ( { uUID, tOffset } );
	assert(ppBlock);

	LinkedBlock_t * pBlock = *ppBlock;
	pBlock->m_iRefcount--;
	assert ( pBlock->m_iRefcount>=0 );
}


void BlockCache_c::DeleteAll ( DWORD uUID )
{
	LinkedBlock_t * pBlock = m_pHead;
	while ( pBlock )
	{
		if ( pBlock->m_tKey.m_uUID==uUID )
		{
			assert ( !pBlock->m_iRefcount );
			LinkedBlock_t * pToDelete = pBlock;
			pBlock = pBlock->m_pNext;
			Delete(pToDelete);
		}
		else
			pBlock = pBlock->m_pNext;
	}
}


bool BlockCache_c::Add ( DWORD uUID, SphOffset_t tOffset, BlockData_t & tData )
{
	DWORD uSpaceNeeded = tData.m_uSize + sizeof(LinkedBlock_t);
	if ( !HaveSpaceFor ( uSpaceNeeded ) )
	{
		DWORD MAX_BLOCK_SIZE = m_iCacheSize/64;
		if ( uSpaceNeeded>MAX_BLOCK_SIZE )
			return false;

		SweepUnused ( uSpaceNeeded );
		if ( !HaveSpaceFor ( uSpaceNeeded ) )
			return false;
	}

#ifndef NDEBUG
	LinkedBlock_t ** ppBlock = m_tHash.Find ( { uUID, tOffset } );
	assert ( !ppBlock );
#endif

	LinkedBlock_t * pBlock = new LinkedBlock_t;
	*(BlockData_t*)pBlock = tData;
	pBlock->m_iRefcount++;
	pBlock->m_tKey = { uUID, tOffset };

	Add ( pBlock );	
	return true;
}


void BlockCache_c::SweepUnused ( DWORD uSpaceNeeded )
{
	// least recently used blocks are the tail
	LinkedBlock_t * pBlock = m_pTail;
	while ( pBlock && !HaveSpaceFor ( uSpaceNeeded ) )
	{
		if ( !pBlock->m_iRefcount )
		{
			assert ( !pBlock->m_iRefcount );
			LinkedBlock_t * pToDelete = pBlock;
			pBlock = pBlock->m_pPrev;
			Delete(pToDelete);
		}
		else
			pBlock = pBlock->m_pPrev;
	}
}


bool BlockCache_c::HaveSpaceFor ( DWORD uSpaceNeeded ) const
{
	return m_iMemUsed+uSpaceNeeded <= m_iCacheSize;
}


void BlockCache_c::Init ( int iCacheSize )
{
	assert ( !m_pBlockCache );
	if ( iCacheSize > 0 )
		m_pBlockCache = new BlockCache_c(iCacheSize);
}


void BlockCache_c::Done()
{
	SafeDelete(m_pBlockCache);
}


BlockCache_c * BlockCache_c::Get()
{
	return m_pBlockCache;
}

//////////////////////////////////////////////////////////////////////////

class DocstoreReaders_c
{
public:
						~DocstoreReaders_c();

	void				CreateReader ( int64_t iSessionId, DWORD uDocstoreId, const CSphAutofile & tFile, DWORD uBlockSize );
	CSphReader *		GetReader ( int64_t iSessionId, DWORD uDocstoreId );
	void				DeleteBySessionId ( int64_t iSessionId );
	void				DeleteByDocstoreId ( DWORD uDocstoreId );

	static void					Init();
	static void					Done();
	static DocstoreReaders_c *	Get();

private:
	struct HashKey_t
	{
		int64_t		m_iSessionId;
		DWORD		m_uDocstoreId;
		
		bool operator == ( const HashKey_t & tKey ) const;
		static DWORD GetHash ( const HashKey_t & tKey );
	};

	int			m_iTotalReaderSize = 0;
	CSphMutex	m_tLock;
	OpenHash_T<CSphReader *, HashKey_t, HashKey_t> m_tHash;

	static DocstoreReaders_c * m_pReaders;

	static const int MIN_READER_CACHE_SIZE = 262144;
	static const int MAX_READER_CACHE_SIZE = 1048576;
	static const int MAX_TOTAL_READER_SIZE = 8388608;

	void		Delete ( CSphReader * pReader, const HashKey_t tKey );
};


DocstoreReaders_c * DocstoreReaders_c::m_pReaders = nullptr;


bool DocstoreReaders_c::HashKey_t::operator == ( const HashKey_t & tKey ) const
{
	return m_iSessionId==tKey.m_iSessionId && m_uDocstoreId==tKey.m_uDocstoreId;
}


DWORD DocstoreReaders_c::HashKey_t::GetHash ( const HashKey_t & tKey )
{
	DWORD uCRC32 = sphCRC32 ( &tKey.m_iSessionId, sizeof(tKey.m_iSessionId) );
	return sphCRC32 ( &tKey.m_uDocstoreId, sizeof(tKey.m_uDocstoreId), uCRC32 );
}


DocstoreReaders_c::~DocstoreReaders_c()
{
	int64_t iIndex = 0;
	HashKey_t tKey;
	CSphReader ** ppReader;
	while ( (ppReader = m_tHash.Iterate ( &iIndex, &tKey )) != nullptr )
		SafeDelete ( *ppReader );
}


void DocstoreReaders_c::CreateReader ( int64_t iSessionId, DWORD uDocstoreId, const CSphAutofile & tFile, DWORD uBlockSize )
{
	int iBufferSize = (int)uBlockSize*8;
	iBufferSize = Min ( iBufferSize, MAX_READER_CACHE_SIZE );
	iBufferSize = Max ( iBufferSize, MIN_READER_CACHE_SIZE );

	if ( iBufferSize<=(int)uBlockSize )
		return;

	if ( m_iTotalReaderSize+iBufferSize > MAX_TOTAL_READER_SIZE )
		return;

	CSphReader * pReader = new CSphReader ( nullptr, iBufferSize );
	pReader->SetFile(tFile);

	ScopedMutex_t tLock(m_tLock);
	Verify ( m_tHash.Add ( {iSessionId, uDocstoreId}, pReader ) );
	m_iTotalReaderSize += iBufferSize;
}


CSphReader * DocstoreReaders_c::GetReader ( int64_t iSessionId, DWORD uDocstoreId )
{
	CSphReader ** ppReader = m_tHash.Find ( { iSessionId, uDocstoreId } );
	return ppReader ? *ppReader : nullptr;
}


void DocstoreReaders_c::Delete ( CSphReader * pReader, const HashKey_t tKey )
{
	m_iTotalReaderSize -= pReader->GetBufferSize();
	assert ( m_iTotalReaderSize>=0 );

	SafeDelete(pReader);
	m_tHash.Delete(tKey);
}


void DocstoreReaders_c::DeleteBySessionId ( int64_t iSessionId )
{
	ScopedMutex_t tLock(m_tLock);

	// fixme: create a separate (faster) lookup?
	int64_t iIndex = 0;
	HashKey_t tKey;
	CSphReader ** ppReader;
	while ( (ppReader = m_tHash.Iterate ( &iIndex, &tKey )) != nullptr )
		if ( tKey.m_iSessionId==iSessionId )
			Delete ( *ppReader, tKey );
}


void DocstoreReaders_c::DeleteByDocstoreId ( DWORD uDocstoreId )
{
	ScopedMutex_t tLock(m_tLock);

	// fixme: create a separate (faster) lookup?
	int64_t iIndex = 0;
	HashKey_t tKey;
	CSphReader ** ppReader;
	while ( (ppReader = m_tHash.Iterate ( &iIndex, &tKey )) != nullptr )
		if ( tKey.m_uDocstoreId==uDocstoreId )
			Delete ( *ppReader, tKey );
}


void DocstoreReaders_c::Init ()
{
	assert(!m_pReaders);
	m_pReaders = new DocstoreReaders_c;
}


void DocstoreReaders_c::Done()
{
	SafeDelete(m_pReaders);
}


DocstoreReaders_c * DocstoreReaders_c::Get()
{
	return m_pReaders;
}

//////////////////////////////////////////////////////////////////////////

static void CreateFieldRemap ( VecTraits_T<int> & dFieldInRset, const VecTraits_T<int> * pFieldIds )
{
	ARRAY_FOREACH ( i, dFieldInRset )
	{
		if ( !pFieldIds )
			dFieldInRset[i] = i;
		else
		{
			int * pFound = pFieldIds->BinarySearch(i);
			dFieldInRset[i] = pFound ? (pFound-pFieldIds->Begin()) : -1;
		}
	}
}


//////////////////////////////////////////////////////////////////////////

class Docstore_c : public Docstore_i, public DocstoreSettings_t
{
public:
						Docstore_c ( const CSphString & sFilename );
						~Docstore_c();

	bool				Init ( CSphString & sError );

	int					AddField ( const CSphString & sName, DocstoreDataType_e eType ) final;
	int					GetFieldId ( const CSphString & sName, DocstoreDataType_e eType ) const final;
	void				CreateReader ( int64_t iSessionId ) const final;
	DocstoreDoc_t		GetDoc ( RowID_t tRowID, const VecTraits_T<int> * pFieldIds, int64_t iSessionId, bool bPack ) const final;
	DocstoreSettings_t	GetDocstoreSettings() const final;

private:
	struct Block_t
	{
		SphOffset_t	m_tOffset = 0;
		DWORD		m_uSize = 0;
		DWORD		m_uHeaderSize = 0;
		RowID_t		m_tRowID = INVALID_ROWID;
		BlockType_e m_eType = BLOCK_TYPE_SMALL;
	};

	struct FieldInfo_t
	{
		BYTE	m_uFlags = 0;
		DWORD	m_uCompressedLen = 0;
		DWORD	m_uUncompressedLen = 0;
	};


	DWORD						m_uUID = 0;
	CSphString					m_sFilename;
	CSphAutofile				m_tFile;
	CSphFixedVector<Block_t>	m_dBlocks{0};
	CSphScopedPtr<Compressor_i> m_pCompressor{nullptr};
	DocstoreFields_c			m_tFields;

	static DWORD				m_uUIDGenerator;

	const Block_t *				FindBlock ( RowID_t tRowID ) const;
	void						ReadFromFile ( BYTE * pData, int iLength, SphOffset_t tOffset, int64_t iSessionId ) const;
	DocstoreDoc_t				ReadDocFromSmallBlock ( const Block_t & tBlock, RowID_t tRowID, const VecTraits_T<int> * pFieldIds, int64_t iSessionId, bool bPack ) const;
	DocstoreDoc_t				ReadDocFromBigBlock ( const Block_t & tBlock, const VecTraits_T<int> * pFieldIds, int64_t iSessionId, bool bPack ) const;
	BlockCache_c::BlockData_t	UncompressSmallBlock ( const Block_t & tBlock, int64_t iSessionId ) const;
	BlockCache_c::BlockData_t	UncompressBigBlockField ( SphOffset_t tOffset, const FieldInfo_t & tInfo, int64_t iSessionId ) const;

	bool						ProcessSmallBlockDoc ( RowID_t tCurDocRowID, RowID_t tRowID, const VecTraits_T<int> * pFieldIds, const CSphFixedVector<int> & dFieldInRset, bool bPack,
		MemoryReader2_c & tReader, CSphBitvec & tEmptyFields, DocstoreDoc_t & tResult ) const;
	const void					ProcessBigBlockField ( int iField, const FieldInfo_t & tInfo, int iFieldInRset, bool bPack, int64_t iSessionId, SphOffset_t & tOffset, DocstoreDoc_t & tResult ) const;
};


DWORD Docstore_c::m_uUIDGenerator = 0;


Docstore_c::Docstore_c ( const CSphString & sFilename )
	: m_sFilename ( sFilename )
{
	m_uUID = m_uUIDGenerator++;
}


Docstore_c::~Docstore_c ()
{
	BlockCache_c * pBlockCache = BlockCache_c::Get();
	if ( pBlockCache )
		pBlockCache->DeleteAll(m_uUID);

	DocstoreReaders_c * pReaders = DocstoreReaders_c::Get();
	if ( pReaders )
		pReaders->DeleteByDocstoreId(m_uUID);
}


bool Docstore_c::Init ( CSphString & sError )
{
	CSphAutoreader tReader;
	if ( !tReader.Open ( m_sFilename, sError ) )
		return false;

	DWORD uStorageVersion = tReader.GetDword();
	if ( uStorageVersion > STORAGE_VERSION )
	{
		sError.SetSprintf ( "Unable to load docstore: %s is v.%d, binary is v.%d", m_sFilename.cstr(), uStorageVersion, STORAGE_VERSION );
		return false;
	}

	m_uBlockSize = tReader.GetDword();
	m_eCompression = Byte2Compression ( tReader.GetByte() );

	m_pCompressor = CreateCompressor ( m_eCompression, m_iCompressionLevel );
	if ( !m_pCompressor )
		return false;

	m_tFields.Load(tReader);

	DWORD uNumBlocks = tReader.GetDword();
	assert(uNumBlocks);
	SphOffset_t tHeaderOffset = tReader.GetOffset();

	tReader.SeekTo ( tHeaderOffset, 0 );

	m_dBlocks.Reset(uNumBlocks);
	DWORD tPrevBlockRowID = 0;
	SphOffset_t tPrevBlockOffset = 0;
	for ( auto & i : m_dBlocks )
	{
		i.m_tRowID = tReader.UnzipRowid() + tPrevBlockRowID;
		i.m_eType = (BlockType_e)tReader.GetByte();
		i.m_tOffset = tReader.UnzipOffset() + tPrevBlockOffset;
		if ( i.m_eType==BLOCK_TYPE_BIG )
			i.m_uHeaderSize = tReader.UnzipInt();

		tPrevBlockRowID = i.m_tRowID;
		tPrevBlockOffset = i.m_tOffset;
	}

	for ( int i = 1; i<m_dBlocks.GetLength(); i++ )
		m_dBlocks[i-1].m_uSize = m_dBlocks[i].m_tOffset-m_dBlocks[i-1].m_tOffset;

	m_dBlocks.Last().m_uSize = tHeaderOffset-m_dBlocks.Last().m_tOffset;

	if ( tReader.GetErrorFlag() )
		return false;

	tReader.Close();

	if ( m_tFile.Open ( m_sFilename, SPH_O_READ, sError ) < 0 )
		return false;

	return true;
}


const Docstore_c::Block_t * Docstore_c::FindBlock ( RowID_t tRowID ) const
{
	const Block_t * pFound = sphBinarySearchFirst ( m_dBlocks.Begin(), m_dBlocks.End()-1, bind(&Block_t::m_tRowID), tRowID );
	assert(pFound);

	if ( pFound->m_tRowID>tRowID )
	{
		if ( pFound==m_dBlocks.Begin() )
			return nullptr;

		return pFound-1;
	}

	return pFound;
}


int Docstore_c::AddField ( const CSphString & sName, DocstoreDataType_e eType )
{
	assert ( 0 && "adding fields to docstore" );
	return -1;
}


void Docstore_c::CreateReader ( int64_t iSessionId ) const
{
	DocstoreReaders_c * pReaders = DocstoreReaders_c::Get();
	if ( pReaders )
		pReaders->CreateReader ( iSessionId, m_uUID, m_tFile, m_uBlockSize );
}


int Docstore_c::GetFieldId ( const CSphString & sName, DocstoreDataType_e eType ) const
{
	return m_tFields.GetFieldId (sName, eType );
}


DocstoreDoc_t Docstore_c::GetDoc ( RowID_t tRowID, const VecTraits_T<int> * pFieldIds, int64_t iSessionId, bool bPack ) const
{
#ifndef NDEBUG
	// assume that field ids are sorted
	for ( int i = 1; pFieldIds && i < pFieldIds->GetLength(); i++ )
		assert ( (*pFieldIds)[i-1] < (*pFieldIds)[i] );
#endif

	const Block_t * pBlock = FindBlock(tRowID);
	assert ( pBlock );

	if ( pBlock->m_eType==BLOCK_TYPE_SMALL )
		return ReadDocFromSmallBlock ( *pBlock, tRowID, pFieldIds, iSessionId, bPack );
	else
		return ReadDocFromBigBlock ( *pBlock, pFieldIds, iSessionId, bPack );
}


struct ScopedBlock_t
{
	DWORD		m_uUID = UINT_MAX;
	SphOffset_t	m_tOffset = 0;

	~ScopedBlock_t()
	{
		if ( m_uUID==UINT_MAX )
			return;

		BlockCache_c * pBlockCache = BlockCache_c::Get();
		assert ( pBlockCache );
		pBlockCache->Release ( m_uUID, m_tOffset );
	}
};


void Docstore_c::ReadFromFile ( BYTE * pData, int iLength, SphOffset_t tOffset, int64_t iSessionId ) const
{
	DocstoreReaders_c * pReaders = DocstoreReaders_c::Get();
	CSphReader * pReader = nullptr;
	if ( pReaders )
		pReader = pReaders->GetReader ( iSessionId, m_uUID );

	if ( pReader )
	{
		pReader->SeekTo ( tOffset, iLength );
		pReader->GetBytes ( pData, iLength );
	}
	else
		sphPread ( m_tFile.GetFD(), pData, iLength, tOffset );
}


BlockCache_c::BlockData_t Docstore_c::UncompressSmallBlock ( const Block_t & tBlock, int64_t iSessionId ) const
{
	BlockCache_c::BlockData_t tResult;
	CSphFixedVector<BYTE> dBlock ( tBlock.m_uSize );

	ReadFromFile ( dBlock.Begin(), dBlock.GetLength(), tBlock.m_tOffset, iSessionId );

	MemoryReader2_c tBlockReader ( dBlock.Begin(), dBlock.GetLength() );
	tResult.m_uFlags = tBlockReader.GetByte();
	tResult.m_uNumDocs = tBlockReader.UnzipInt();
	tResult.m_uSize = tBlockReader.UnzipInt();
	DWORD uCompressedLength = tResult.m_uSize;
	bool bCompressed = tResult.m_uFlags & BLOCK_FLAG_COMPRESSED;
	if ( bCompressed )
		uCompressedLength = tBlockReader.UnzipInt();

	const BYTE * pBody = dBlock.Begin() + tBlockReader.GetPos();

	CSphFixedVector<BYTE> dDecompressed(0);
	if ( bCompressed )
	{
		dDecompressed.Reset ( tResult.m_uSize );
		Verify ( m_pCompressor->Decompress ( VecTraits_T<const BYTE> (pBody, uCompressedLength), dDecompressed) );
		tResult.m_pData = dDecompressed.LeakData();
	}
	else
	{
		// we can't just pass tResult.m_pData because it doesn't point to the start of the allocated block
		tResult.m_pData = new BYTE[tResult.m_uSize];
		memcpy ( tResult.m_pData, pBody, tResult.m_uSize );
	}

	return tResult;
}


bool Docstore_c::ProcessSmallBlockDoc ( RowID_t tCurDocRowID, RowID_t tRowID, const VecTraits_T<int> * pFieldIds, const CSphFixedVector<int> & dFieldInRset, bool bPack,
	MemoryReader2_c & tReader, CSphBitvec & tEmptyFields, DocstoreDoc_t & tResult ) const
{
	bool bDocFound = tCurDocRowID==tRowID;
	if ( bDocFound )
		tResult.m_dFields.Resize ( pFieldIds ? pFieldIds->GetLength() : m_tFields.GetNumFields() );

	DWORD uBitMaskSize = tEmptyFields.GetSize()*sizeof(DWORD);

	BYTE uDocFlags = tReader.GetByte();
	if ( uDocFlags & DOC_FLAG_ALL_EMPTY )
	{
		for ( auto & i : tResult.m_dFields )
			i.Resize(0);

		return bDocFound;
	}

	bool bHasBitmask = !!(uDocFlags & DOC_FLAG_EMPTY_BITMASK);
	if ( bHasBitmask )
	{
		memcpy ( tEmptyFields.Begin(), tReader.Begin()+tReader.GetPos(), uBitMaskSize );
		tReader.SetPos ( tReader.GetPos()+uBitMaskSize );
	}

	for ( int iField = 0; iField < m_tFields.GetNumFields(); iField++ )
		if ( !bHasBitmask || !tEmptyFields.BitGet(iField) )
		{
			DWORD uFieldLength = tReader.UnzipInt();
			int iFieldInRset = dFieldInRset[iField];
			if ( bDocFound && iFieldInRset!=-1 )
				PackData ( tResult.m_dFields[iFieldInRset], tReader.Begin()+tReader.GetPos(), uFieldLength, m_tFields.GetField(iField).m_eType==DOCSTORE_TEXT, bPack );

			tReader.SetPos ( tReader.GetPos()+uFieldLength );
		}

	return bDocFound;
}


DocstoreDoc_t Docstore_c::ReadDocFromSmallBlock ( const Block_t & tBlock, RowID_t tRowID, const VecTraits_T<int> * pFieldIds, int64_t iSessionId, bool bPack ) const
{
	BlockCache_c * pBlockCache = BlockCache_c::Get();

	BlockCache_c::BlockData_t tBlockData;
	bool bFromCache = pBlockCache && pBlockCache->Find ( m_uUID, tBlock.m_tOffset, tBlockData );
	if ( !bFromCache )
	{
		tBlockData = UncompressSmallBlock ( tBlock, iSessionId );
		bFromCache = pBlockCache && pBlockCache->Add ( m_uUID, tBlock.m_tOffset, tBlockData );
	}

	ScopedBlock_t tScopedBlock;
	CSphScopedPtr<BYTE> tDataPtr(nullptr);
	if ( bFromCache )
	{
		tScopedBlock.m_uUID = m_uUID;
		tScopedBlock.m_tOffset = tBlock.m_tOffset;
	}
	else
		tDataPtr = tBlockData.m_pData;

	CSphFixedVector<int> dFieldInRset (	m_tFields.GetNumFields() );
	CreateFieldRemap ( dFieldInRset, pFieldIds );

	DocstoreDoc_t tResult;

	RowID_t tCurDocRowID = tBlock.m_tRowID;
	MemoryReader2_c tReader ( tBlockData.m_pData, tBlockData.m_uSize );
	CSphBitvec tEmptyFields ( m_tFields.GetNumFields() );
	for ( int i = 0; i < (int)tBlockData.m_uNumDocs; i++ )
	{
		if ( ProcessSmallBlockDoc ( tCurDocRowID, tRowID, pFieldIds, dFieldInRset, bPack, tReader, tEmptyFields, tResult ) )
			break;

		tCurDocRowID++;
	}

	return tResult;
}


BlockCache_c::BlockData_t Docstore_c::UncompressBigBlockField ( SphOffset_t tOffset, const FieldInfo_t & tInfo, int64_t iSessionId ) const
{
	BlockCache_c::BlockData_t tResult;
	bool bCompressed = !!( tInfo.m_uFlags & FIELD_FLAG_COMPRESSED );
	DWORD uDataLen = bCompressed ? tInfo.m_uCompressedLen : tInfo.m_uUncompressedLen;

	CSphFixedVector<BYTE> dField ( uDataLen );
	ReadFromFile ( dField.Begin(), dField.GetLength(), tOffset, iSessionId );

	tResult.m_uSize = tInfo.m_uUncompressedLen;

	CSphFixedVector<BYTE> dDecompressed(0);
	if ( bCompressed )
	{
		dDecompressed.Reset ( tResult.m_uSize );
		Verify ( m_pCompressor->Decompress ( dField, dDecompressed ) );
		tResult.m_pData = dDecompressed.LeakData();
	}
	else
		tResult.m_pData = dField.LeakData();

	return tResult;
}


const void Docstore_c::ProcessBigBlockField ( int iField, const FieldInfo_t & tInfo, int iFieldInRset, bool bPack, int64_t iSessionId, SphOffset_t & tOffset, DocstoreDoc_t & tResult ) const
{
	if ( tInfo.m_uFlags & FIELD_FLAG_EMPTY )
		return;

	bool bCompressed = !!( tInfo.m_uFlags & FIELD_FLAG_COMPRESSED );
	SphOffset_t tOffsetDelta = bCompressed ? tInfo.m_uCompressedLen : tInfo.m_uUncompressedLen;
	if ( iFieldInRset==-1 )
	{
		tOffset += tOffsetDelta;
		return;
	}

	BlockCache_c * pBlockCache = BlockCache_c::Get();

	BlockCache_c::BlockData_t tBlockData;
	bool bFromCache = pBlockCache && pBlockCache->Find ( m_uUID, tOffset, tBlockData );
	if ( !bFromCache )
	{
		tBlockData = UncompressBigBlockField ( tOffset, tInfo, iSessionId );
		bFromCache = pBlockCache && pBlockCache->Add ( m_uUID, tOffset, tBlockData );
	}

	ScopedBlock_t tScopedBlock;
	CSphScopedPtr<BYTE> tDataPtr(nullptr);
	if ( bFromCache )
	{
		tScopedBlock.m_uUID = m_uUID;
		tScopedBlock.m_tOffset = tOffset;
	}
	else
		tDataPtr = tBlockData.m_pData;

	PackData ( tResult.m_dFields[iFieldInRset], tBlockData.m_pData, tBlockData.m_uSize, m_tFields.GetField(iField).m_eType==DOCSTORE_TEXT, bPack );

	tOffset += tOffsetDelta;
}


DocstoreDoc_t Docstore_c::ReadDocFromBigBlock ( const Block_t & tBlock, const VecTraits_T<int> * pFieldIds, int64_t iSessionId, bool bPack ) const
{
	CSphFixedVector<FieldInfo_t> dFieldInfo ( m_tFields.GetNumFields() );
	CSphFixedVector<BYTE> dBlockHeader(tBlock.m_uHeaderSize);

	ReadFromFile ( dBlockHeader.Begin(), dBlockHeader.GetLength(), tBlock.m_tOffset, iSessionId );
	MemoryReader2_c tReader ( dBlockHeader.Begin(), dBlockHeader.GetLength() );

	CSphVector<int> dFieldSort;
	BYTE uBlockFlags = tReader.GetByte();
	bool bNeedReorder = !!( uBlockFlags & BLOCK_FLAG_FIELD_REORDER );
	if ( bNeedReorder )
	{
		dFieldSort.Resize ( m_tFields.GetNumFields() );
		for ( auto & i : dFieldSort )
			i = tReader.UnzipInt();
	}

	for ( int i = 0; i < m_tFields.GetNumFields(); i++ )
	{
		int iField = bNeedReorder ? dFieldSort[i] : i;
		FieldInfo_t & tInfo = dFieldInfo[iField];

		tInfo.m_uFlags = tReader.GetByte();
		if ( tInfo.m_uFlags & FIELD_FLAG_EMPTY )
			continue;

		tInfo.m_uUncompressedLen = tReader.UnzipInt();
		if ( tInfo.m_uFlags & FIELD_FLAG_COMPRESSED )
			tInfo.m_uCompressedLen = tReader.UnzipInt();
	}

	dBlockHeader.Reset(0);

	CSphFixedVector<int> dFieldInRset ( m_tFields.GetNumFields() );
	CreateFieldRemap ( dFieldInRset, pFieldIds );

	DocstoreDoc_t tResult;
	tResult.m_dFields.Resize ( pFieldIds ? pFieldIds->GetLength() : m_tFields.GetNumFields() );

	SphOffset_t tOffset = tBlock.m_tOffset+tBlock.m_uHeaderSize;

	// i == physical field order in file
	// dFieldSort[i] == field order as in m_dFields
	// dFieldInRset[iField] == field order in result set
	for ( int i = 0; i < m_tFields.GetNumFields(); i++ )
	{
		int iField = bNeedReorder ? dFieldSort[i] : i;
		ProcessBigBlockField ( iField, dFieldInfo[iField], dFieldInRset[iField], bPack, iSessionId, tOffset, tResult );
	}

	return tResult;
}


DocstoreSettings_t Docstore_c::GetDocstoreSettings() const
{
	return *this;
}

//////////////////////////////////////////////////////////////////////////
DocstoreBuilder_i::Doc_t::Doc_t()
{}

DocstoreBuilder_i::Doc_t::Doc_t ( const DocstoreDoc_t & tDoc )
{
	m_dFields.Resize ( tDoc.m_dFields.GetLength() );
	ARRAY_FOREACH ( i, m_dFields )
		m_dFields[i] = VecTraits_T<BYTE> ( tDoc.m_dFields[i].Begin(), tDoc.m_dFields[i].GetLength() );
}

//////////////////////////////////////////////////////////////////////////

class DocstoreBuilder_c : public DocstoreBuilder_i, public DocstoreSettings_t
{
public:
			DocstoreBuilder_c ( const CSphString & sFilename, const DocstoreSettings_t & tSettings );

	bool	Init ( CSphString & sError );

	void	AddDoc ( RowID_t tRowID, const Doc_t & tDoc ) final;
	int		AddField ( const CSphString & sName, DocstoreDataType_e eType ) final;
	int		GetFieldId ( const CSphString & sName, DocstoreDataType_e eType ) const final;
	void	Finalize() final;

private:
	struct StoredDoc_t
	{
		RowID_t							m_tRowID;
		CSphVector<CSphVector<BYTE>>	m_dFields;
	};

	CSphString				m_sFilename;
	CSphVector<StoredDoc_t>	m_dStoredDocs;
	CSphVector<BYTE>		m_dHeader;
	CSphVector<BYTE>		m_dBuffer;
	CSphScopedPtr<Compressor_i> m_pCompressor{nullptr};
	MemoryWriter2_c			m_tHeaderWriter;
	CSphWriter				m_tWriter;
	DocstoreFields_c		m_tFields;
	DWORD					m_uStoredLen = 0;
	int						m_iNumBlocks = 0;
	SphOffset_t				m_tHeaderOffset = 0;
	SphOffset_t				m_tPrevBlockOffset = 0;
	DWORD					m_tPrevBlockRowID = 0;

	using SortedField_t = std::pair<int,int>;
	CSphVector<SortedField_t>		m_dFieldSort;
	CSphVector<CSphVector<BYTE>>	m_dCompressedBuffers;

	void	WriteInitialHeader();
	void	WriteTrailingHeader();
	void	WriteBlock();
	void	WriteSmallBlockHeader ( SphOffset_t tBlockOffset );
	void	WriteBigBlockHeader ( SphOffset_t tBlockOffset, SphOffset_t tHeaderSize );
	void	WriteSmallBlock();
	void	WriteBigBlock();
};


DocstoreBuilder_c::DocstoreBuilder_c ( const CSphString & sFilename, const DocstoreSettings_t & tSettings )
	: m_sFilename ( sFilename )
	, m_tHeaderWriter ( m_dHeader )
{
	*(DocstoreSettings_t*)this = tSettings;
}


bool DocstoreBuilder_c::Init ( CSphString & sError )
{
	m_pCompressor = CreateCompressor ( m_eCompression, m_iCompressionLevel );
	if ( !m_pCompressor )
		return false;

	if ( !m_tWriter.OpenFile ( m_sFilename, sError ) )
		return false;

	return true;
}


void DocstoreBuilder_c::AddDoc ( RowID_t tRowID, const Doc_t & tDoc )
{
	assert ( tDoc.m_dFields.GetLength()==m_tFields.GetNumFields() );

	DWORD uLen = 0;
	for ( const auto & i : tDoc.m_dFields )
		uLen += i.GetLength();

	if ( m_uStoredLen+uLen > m_uBlockSize )
		WriteBlock();

	StoredDoc_t & tStoredDoc = m_dStoredDocs.Add();
	tStoredDoc.m_tRowID = tRowID;
	tStoredDoc.m_dFields.Resize ( m_tFields.GetNumFields() );
	for ( int i = 0; i<m_tFields.GetNumFields(); i++ )
	{
		int iLen = tDoc.m_dFields[i].GetLength();

		// remove trailing zero
		if ( m_tFields.GetField(i).m_eType==DOCSTORE_TEXT && iLen>0 && tDoc.m_dFields[i][iLen-1]=='\0' )
			iLen--;

		tStoredDoc.m_dFields[i].Resize(iLen);
		memcpy ( tStoredDoc.m_dFields[i].Begin(), tDoc.m_dFields[i].Begin(), iLen );
	}

	m_uStoredLen += uLen;
}


int DocstoreBuilder_c::AddField ( const CSphString & sName, DocstoreDataType_e eType )
{
	return m_tFields.AddField ( sName, eType );
}


int DocstoreBuilder_c::GetFieldId ( const CSphString & sName, DocstoreDataType_e eType ) const
{
	assert ( 0 && "getting field id from docstore builder" );
	return -1;
}


void DocstoreBuilder_c::Finalize()
{
	WriteBlock();
	WriteTrailingHeader();
}


void DocstoreBuilder_c::WriteInitialHeader()
{
	m_tWriter.PutDword ( STORAGE_VERSION );
	m_tWriter.PutDword ( m_uBlockSize );
	m_tWriter.PutByte ( Compression2Byte(m_eCompression) );
	m_tFields.Save(m_tWriter);

	m_tHeaderOffset = m_tWriter.GetPos();

	// reserve space for number of blocks
	m_tWriter.PutDword(0);

	// reserve space for header offset
	m_tWriter.PutOffset(0);
}


void DocstoreBuilder_c::WriteTrailingHeader()
{
	SphOffset_t tHeaderPos = m_tWriter.GetPos();

	// write header
	m_tWriter.PutBytes ( m_dHeader.Begin(), m_dHeader.GetLength() );

	// rewind to the beginning, store num_blocks, offset to header
	m_tWriter.Flush();	// flush is necessary, see similar code in BlobRowBuilder_File_c::Done
	m_tWriter.SeekTo(m_tHeaderOffset); 
	m_tWriter.PutDword(m_iNumBlocks);
	m_tWriter.PutOffset(tHeaderPos);
	m_tWriter.CloseFile();
}


void DocstoreBuilder_c::WriteSmallBlockHeader ( SphOffset_t tBlockOffset )
{
	m_tHeaderWriter.ZipInt ( m_dStoredDocs[0].m_tRowID-m_tPrevBlockRowID );		// initial block rowid delta
	m_tHeaderWriter.PutByte ( BLOCK_TYPE_SMALL );								// block type
	m_tHeaderWriter.ZipOffset ( tBlockOffset-m_tPrevBlockOffset );				// block offset	delta

	m_tPrevBlockOffset = tBlockOffset;
	m_tPrevBlockRowID = m_dStoredDocs[0].m_tRowID;
}


void DocstoreBuilder_c::WriteBigBlockHeader ( SphOffset_t tBlockOffset, SphOffset_t tHeaderSize )
{
	m_tHeaderWriter.ZipInt ( m_dStoredDocs[0].m_tRowID-m_tPrevBlockRowID );		// initial block rowid delta
	m_tHeaderWriter.PutByte ( BLOCK_TYPE_BIG );									// block type
	m_tHeaderWriter.ZipOffset ( tBlockOffset-m_tPrevBlockOffset );				// block offset	delta
	m_tHeaderWriter.ZipInt ( tHeaderSize );										// on-disk header size

	m_tPrevBlockOffset = tBlockOffset;
	m_tPrevBlockRowID = m_dStoredDocs[0].m_tRowID;
}


void DocstoreBuilder_c::WriteSmallBlock()
{
	m_dCompressedBuffers.Resize(1);
	m_dBuffer.Resize(0);
	MemoryWriter2_c tMemWriter ( m_dBuffer );

#ifndef NDEBUG
	for ( int i=1; i < m_dStoredDocs.GetLength(); i++ )
		assert ( m_dStoredDocs[i].m_tRowID-m_dStoredDocs[i-1].m_tRowID==1 );
#endif // !NDEBUG

	CSphBitvec tEmptyFields ( m_tFields.GetNumFields() );
	for ( const auto & tDoc : m_dStoredDocs )
	{
		tEmptyFields.Clear();
		ARRAY_FOREACH ( iField, tDoc.m_dFields )
			if ( !tDoc.m_dFields[iField].GetLength() )
				tEmptyFields.BitSet(iField);

		int iEmptyFields = tEmptyFields.BitCount();
		if ( iEmptyFields==m_tFields.GetNumFields() )
			tMemWriter.PutByte ( DOC_FLAG_ALL_EMPTY );
		else
		{
			bool bNeedsBitmask = iEmptyFields && ( tEmptyFields.GetSize()*sizeof(*tEmptyFields.Begin()) < (DWORD)iEmptyFields );

			tMemWriter.PutByte ( bNeedsBitmask ? DOC_FLAG_EMPTY_BITMASK : 0 );
			if ( bNeedsBitmask )
				tMemWriter.PutBytes ( tEmptyFields.Begin(), tEmptyFields.GetSize()*sizeof(*tEmptyFields.Begin()) );
			
			ARRAY_FOREACH ( iField, tDoc.m_dFields )
				if ( !bNeedsBitmask || !tEmptyFields.BitGet(iField) )
				{
					const CSphVector<BYTE> & tField = tDoc.m_dFields[iField];
					tMemWriter.ZipInt ( tField.GetLength() );
					tMemWriter.PutBytes ( tField.Begin(), tField.GetLength() );
				}
		}
	}

	CSphVector<BYTE> & dCompressedBuffer = m_dCompressedBuffers[0];
	BYTE uBlockFlags = 0;
	bool bCompressed = m_pCompressor->Compress ( m_dBuffer, dCompressedBuffer );
	if ( bCompressed )
		uBlockFlags |= BLOCK_FLAG_COMPRESSED;

	WriteSmallBlockHeader ( m_tWriter.GetPos() );

	m_tWriter.PutByte ( uBlockFlags );									// block flags
	m_tWriter.ZipInt ( m_dStoredDocs.GetLength() );						// num docs
	m_tWriter.ZipInt ( m_dBuffer.GetLength() );							// uncompressed length

	if ( bCompressed )
		m_tWriter.ZipInt ( dCompressedBuffer.GetLength() );		// compressed length

	// body data
	if ( bCompressed )
		m_tWriter.PutBytes ( dCompressedBuffer.Begin(), dCompressedBuffer.GetLength() ); // compressed data
	else
		m_tWriter.PutBytes ( m_dBuffer.Begin(), m_dBuffer.GetLength() ); // uncompressed data
}


void DocstoreBuilder_c::WriteBigBlock()
{
	assert ( m_dStoredDocs.GetLength()==1 );
	StoredDoc_t & tDoc = m_dStoredDocs[0];

	m_dCompressedBuffers.Resize ( m_tFields.GetNumFields() );

	bool bNeedReorder = false;
	CSphBitvec tCompressedFields ( m_tFields.GetNumFields() );
	int iPrevSize = 0;
	ARRAY_FOREACH ( iField, tDoc.m_dFields )
	{
		const CSphVector<BYTE> & dField = tDoc.m_dFields[iField];
		CSphVector<BYTE> & dCompressedBuffer = m_dCompressedBuffers[iField];
		bool bCompressed = m_pCompressor->Compress ( dField, dCompressedBuffer );
		if ( bCompressed )
			tCompressedFields.BitSet(iField);

		int iStoredSize = bCompressed ? dCompressedBuffer.GetLength() : dField.GetLength();
		bNeedReorder |= iStoredSize < iPrevSize;
		iPrevSize = dCompressedBuffer.GetLength();
	}

	if ( bNeedReorder )
	{
		m_dFieldSort.Resize ( m_tFields.GetNumFields() );
		ARRAY_FOREACH ( iField, tDoc.m_dFields )
		{
			m_dFieldSort[iField].first = iField;
			m_dFieldSort[iField].second = tCompressedFields.BitGet(iField) ? m_dCompressedBuffers[iField].GetLength() : tDoc.m_dFields[iField].GetLength();
		}

		m_dFieldSort.Sort ( ::bind(&SortedField_t::second) );
	}

	SphOffset_t tOnDiskHeaderStart = m_tWriter.GetPos();
	BYTE uBlockFlags = bNeedReorder ? BLOCK_FLAG_FIELD_REORDER : 0;
	m_tWriter.PutByte(uBlockFlags);											// block flags

	if ( bNeedReorder )
	{
		for ( const auto & i : m_dFieldSort )
			m_tWriter.ZipInt(i.first);										// field reorder map
	}

	for ( int i = 0; i < m_tFields.GetNumFields(); i++ )
	{
		int iField = bNeedReorder ? m_dFieldSort[i].first : i;
		bool bCompressed = tCompressedFields.BitGet(iField);
		bool bEmpty = !tDoc.m_dFields[iField].GetLength();

		BYTE uFieldFlags = 0;
		uFieldFlags |= bCompressed ? FIELD_FLAG_COMPRESSED : 0;
		uFieldFlags |= bEmpty ? FIELD_FLAG_EMPTY : 0;
		m_tWriter.PutByte(uFieldFlags);										// field flags

		if ( bEmpty )
			continue;

		m_tWriter.ZipInt ( tDoc.m_dFields[iField].GetLength() );			// uncompressed len
		if ( bCompressed )
			m_tWriter.ZipInt ( m_dCompressedBuffers[iField].GetLength() );	// compressed len (if compressed)
	}

	SphOffset_t tOnDiskHeaderSize = m_tWriter.GetPos() - tOnDiskHeaderStart;

	for ( int i = 0; i < m_tFields.GetNumFields(); i++ )
	{
		int iField = bNeedReorder ? m_dFieldSort[i].first : i;
		bool bCompressed = tCompressedFields.BitGet(iField);
		bool bEmpty = !tDoc.m_dFields[iField].GetLength();

		if ( bEmpty )
			continue;

		if ( bCompressed )
			m_tWriter.PutBytes ( m_dCompressedBuffers[iField].Begin(), m_dCompressedBuffers[iField].GetLength() );	// compressed data
		else
			m_tWriter.PutBytes( tDoc.m_dFields[iField].Begin(), tDoc.m_dFields[iField].GetLength() );				// uncompressed data
	}

	WriteBigBlockHeader ( tOnDiskHeaderStart, tOnDiskHeaderSize );
}


void DocstoreBuilder_c::WriteBlock()
{
	if ( !m_tWriter.GetPos() )
		WriteInitialHeader();

	if ( !m_dStoredDocs.GetLength() )
		return;

	bool bBigBlock = m_dStoredDocs.GetLength()==1 && m_uStoredLen>=m_uBlockSize;

	if ( bBigBlock )
		WriteBigBlock();
	else
		WriteSmallBlock();

	m_iNumBlocks++;
	m_uStoredLen = 0;
	m_dStoredDocs.Resize(0);
}

//////////////////////////////////////////////////////////////////////////

class DocstoreRT_c : public DocstoreRT_i
{
public:
						~DocstoreRT_c() override;

	void				AddDoc ( RowID_t tRowID, const DocstoreBuilder_i::Doc_t & tDoc ) final;
	int					AddField ( const CSphString & sName, DocstoreDataType_e eType ) final;
	void				Finalize() final {};

	DocstoreDoc_t		GetDoc ( RowID_t tRowID, const VecTraits_T<int> * pFieldIds, int64_t iSessionId, bool bPack ) const final;
	int					GetFieldId ( const CSphString & sName, DocstoreDataType_e eType ) const final;
	DocstoreSettings_t	GetDocstoreSettings() const final;
	void				CreateReader ( int64_t iSessionId ) const final {};

	bool				Load ( CSphReader & tReader, CSphString & sError ) final;
	void				Save ( CSphWriter & tWriter ) final;

	void				AddPackedDoc ( RowID_t tRowID, BYTE * pDoc ) final;
	BYTE *				LeakPackedDoc ( RowID_t tRowID ) final;

	int64_t				AllocatedBytes() const final;

private:
	CSphVector<BYTE *>	m_dDocs;
	DocstoreFields_c	m_tFields;
	int64_t				m_iAllocated = 0;

	int					GetDocSize ( const BYTE * pDoc ) const;
};


DocstoreRT_c::~DocstoreRT_c()
{
	for ( auto & i : m_dDocs )
		SafeDeleteArray(i);
}


void DocstoreRT_c::AddDoc ( RowID_t tRowID, const DocstoreBuilder_i::Doc_t & tDoc )
{
	assert ( (RowID_t)(m_dDocs.GetLength())==tRowID );

	CSphFixedVector<int> tFieldLengths(tDoc.m_dFields.GetLength());

	int iPackedLen = 0;
	ARRAY_FOREACH ( i, tDoc.m_dFields )
	{
		int iLen = tDoc.m_dFields[i].GetLength();

		// remove trailing zero
		if ( m_tFields.GetField(i).m_eType==DOCSTORE_TEXT && iLen>0 && tDoc.m_dFields[i][iLen-1]=='\0' )
			iLen--;

		iPackedLen += sphCalcZippedLen(iLen)+iLen;
		tFieldLengths[i] = iLen;
	}

	BYTE * & pPacked = m_dDocs.Add();
	pPacked = new BYTE[iPackedLen];
	BYTE * pPtr = pPacked;

	ARRAY_FOREACH ( i, tDoc.m_dFields )
	{
		int iLen = tFieldLengths[i];
		pPtr += sphZipToPtr ( iLen, pPtr );
		memcpy ( pPtr, tDoc.m_dFields[i].Begin(), iLen );
		pPtr += iLen;
	}

	m_iAllocated += iPackedLen;

	assert ( pPtr-pPacked==iPackedLen );
}


int DocstoreRT_c::AddField ( const CSphString & sName, DocstoreDataType_e eType )
{
	return m_tFields.AddField ( sName, eType );
}


DocstoreDoc_t DocstoreRT_c::GetDoc ( RowID_t tRowID, const VecTraits_T<int> * pFieldIds, int64_t iSessionId, bool bPack ) const
{
#ifndef NDEBUG
	// assume that field ids are sorted
	for ( int i = 1; pFieldIds && i < pFieldIds->GetLength(); i++ )
		assert ( (*pFieldIds)[i-1] < (*pFieldIds)[i] );
#endif

	CSphFixedVector<int> dFieldInRset (	m_tFields.GetNumFields() );
	CreateFieldRemap ( dFieldInRset, pFieldIds );

	DocstoreDoc_t tResult;
	tResult.m_dFields.Resize ( pFieldIds ? pFieldIds->GetLength() : m_tFields.GetNumFields() );

	const BYTE * pDoc = m_dDocs[tRowID];
	for ( int iField = 0; iField < m_tFields.GetNumFields(); iField++ )
	{
		DWORD uFieldLength = sphUnzipInt(pDoc);
		int iFieldInRset = dFieldInRset[iField];
		if ( iFieldInRset!=-1 )
			PackData ( tResult.m_dFields[iFieldInRset], pDoc, uFieldLength, m_tFields.GetField(iField).m_eType==DOCSTORE_TEXT, bPack );

		pDoc += uFieldLength;
	}

	return tResult;
}


int DocstoreRT_c::GetFieldId ( const CSphString & sName, DocstoreDataType_e eType ) const
{
	return m_tFields.GetFieldId ( sName, eType );
}


DocstoreSettings_t DocstoreRT_c::GetDocstoreSettings() const
{
	assert ( 0 && "No settings for RT docstore" );
	return DocstoreSettings_t();
}


int DocstoreRT_c::GetDocSize ( const BYTE * pDoc ) const
{
	const BYTE * p = pDoc;
	for ( int iField = 0; iField<m_tFields.GetNumFields(); iField++ )
		p += sphUnzipInt(p);

	return p-pDoc;
}


bool DocstoreRT_c::Load ( CSphReader & tReader, CSphString & sError )
{
	assert ( !m_dDocs.GetLength() && !m_iAllocated );
	DWORD uNumDocs = tReader.UnzipInt();
	m_dDocs.Resize(uNumDocs);
	for ( auto & i : m_dDocs )
	{
		DWORD uDocLen = tReader.UnzipInt();
		i = new BYTE[uDocLen];
		tReader.GetBytes ( i, uDocLen );
		m_iAllocated += uDocLen;
	}

	return !tReader.GetErrorFlag();
}


void DocstoreRT_c::Save ( CSphWriter & tWriter )
{
	tWriter.ZipInt ( m_dDocs.GetLength() );
	for ( const auto & i : m_dDocs )
	{
		int iDocLen = GetDocSize(i);
		tWriter.ZipInt ( iDocLen );
		tWriter.PutBytes ( i, iDocLen );
	}
}


void DocstoreRT_c::AddPackedDoc ( RowID_t tRowID, BYTE * pDoc )
{
	assert ( (RowID_t)(m_dDocs.GetLength())==tRowID );
	m_dDocs.Add(pDoc);
	m_iAllocated += GetDocSize(pDoc);
}


BYTE * DocstoreRT_c::LeakPackedDoc ( RowID_t tRowID )
{
	BYTE * pPacked = m_dDocs[tRowID];
	m_dDocs[tRowID] = nullptr;
	m_iAllocated -= GetDocSize(pPacked);
	assert ( m_iAllocated>=0 );
	return pPacked;
}


int64_t DocstoreRT_c::AllocatedBytes() const
{
	return m_iAllocated + m_dDocs.GetLengthBytes64();
}

//////////////////////////////////////////////////////////////////////////

CSphAtomicL DocstoreSession_c::m_tUIDGenerator;

DocstoreSession_c::DocstoreSession_c()
	: m_iUID ( m_tUIDGenerator.Inc() )
{}


DocstoreSession_c::~DocstoreSession_c()
{
	DocstoreReaders_c * pReaders = DocstoreReaders_c::Get();
	if ( pReaders )
		pReaders->DeleteBySessionId(m_iUID);
}

//////////////////////////////////////////////////////////////////////////

Docstore_i * CreateDocstore ( const CSphString & sFilename, CSphString & sError )
{
	CSphScopedPtr<Docstore_c> pDocstore ( new Docstore_c(sFilename) );
	if ( !pDocstore->Init(sError) )
		return nullptr;

	return pDocstore.LeakPtr();
}


DocstoreBuilder_i * CreateDocstoreBuilder ( const CSphString & sFilename, const DocstoreSettings_t & tSettings, CSphString & sError )
{
	CSphScopedPtr<DocstoreBuilder_c> pBuilder ( new DocstoreBuilder_c ( sFilename, tSettings ) );
	if ( !pBuilder->Init(sError) )
		return nullptr;

	return pBuilder.LeakPtr();
}


DocstoreRT_i * CreateDocstoreRT()
{
	return new DocstoreRT_c;
}


DocstoreFields_i * CreateDocstoreFields()
{
	return new DocstoreFields_c;
}


void InitDocstore ( int iCacheSize )
{
	BlockCache_c::Init(iCacheSize);
	DocstoreReaders_c::Init();
}


void ShutdownDocstore()
{
	BlockCache_c::Done();
	DocstoreReaders_c::Done();
}