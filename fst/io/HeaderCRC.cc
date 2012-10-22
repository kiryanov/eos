//------------------------------------------------------------------------------
// File: HeaderCRC.cc
// Author: Elvin-Alin Sindrilaru - CERN
//------------------------------------------------------------------------------

/************************************************************************
 * EOS - the CERN Disk Storage System                                   *
 * Copyright (C) 2011 CERN/Switzerland                                  *
 *                                                                      *
 * This program is free software: you can redistribute it and/or modify *
 * it under the terms of the GNU General Public License as published by *
 * the Free Software Foundation, either version 3 of the License, or    *
 * (at your option) any later version.                                  *
 *                                                                      *
 * This program is distributed in the hope that it will be useful,      *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of       *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        *
 * GNU General Public License for more details.                         *
 *                                                                      *
 * You should have received a copy of the GNU General Public License    *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.*
 ************************************************************************/

/*----------------------------------------------------------------------------*/
#include <stdint.h>
/*----------------------------------------------------------------------------*/
#include "fst/io/HeaderCRC.hh"
/*----------------------------------------------------------------------------*/

EOSFSTNAMESPACE_BEGIN

int HeaderCRC::msSizeHeader = 4096;             // 4kb
char HeaderCRC::msTagName[] = "_HEADER_RAIDIO_";

//------------------------------------------------------------------------------
// Constructor
//------------------------------------------------------------------------------
HeaderCRC::HeaderCRC():
  mValid( true ),
  mNumBlocks( -1 ),
  mIdStripe( -1 ),
  mSizeLastBlock( -1 )
{
  //empty
}


//------------------------------------------------------------------------------
// Constructor with parameter
//------------------------------------------------------------------------------
HeaderCRC::HeaderCRC( long numBlocks ):
  mValid( true ),
  mNumBlocks( numBlocks ),
  mIdStripe( -1 ),
  mSizeLastBlock( -1 )
{
  strncpy( mTag, msTagName, strlen( msTagName ) );
}


//------------------------------------------------------------------------------
// Destructor
//------------------------------------------------------------------------------
HeaderCRC::~HeaderCRC()
{
  //empty
}


//------------------------------------------------------------------------------
// Read header from file
//------------------------------------------------------------------------------
bool
HeaderCRC::ReadFromFile( XrdCl::File* pFile )
{
  uint32_t ret;
  long int offset = 0;
  char* buff = static_cast< char* >( calloc( msSizeHeader, sizeof( char ) ) );
  eos_debug( "offset: %li, msSizeHeader: %i \n", offset, msSizeHeader );

  if ( !( pFile->Read( offset, msSizeHeader, buff, ret ).IsOK() )
       || ( ret != static_cast< uint32_t >( msSizeHeader ) ) ) {
    free( buff );
    mValid = false;
    return mValid;
  }

  memcpy( mTag, buff, sizeof mTag );

  if ( strncmp( mTag, msTagName, strlen( msTagName ) ) ) {
    free( buff );
    mValid = false;
    return mValid;
  }

  offset += sizeof mTag;
  memcpy( &mIdStripe, buff + offset, sizeof mIdStripe );
  offset += sizeof mIdStripe;
  memcpy( &mNumBlocks, buff + offset, sizeof mNumBlocks );
  offset += sizeof mNumBlocks;
  memcpy( &mSizeLastBlock, buff + offset, sizeof mSizeLastBlock );
  free( buff );
  mValid = true;
  return mValid;
}


//------------------------------------------------------------------------------
// Write header to file
//------------------------------------------------------------------------------
bool
HeaderCRC::WriteToFile( XrdCl::File* pFile )
{
  int offset = 0;
  char* buff = static_cast< char* >( calloc( msSizeHeader, sizeof( char ) ) );
  memcpy( buff + offset, msTagName, sizeof msTagName );
  offset += sizeof mTag;
  memcpy( buff + offset, &mIdStripe, sizeof mIdStripe );
  offset += sizeof mIdStripe;
  memcpy( buff + offset, &mNumBlocks, sizeof mNumBlocks );
  offset += sizeof mNumBlocks;
  memcpy( buff + offset, &mSizeLastBlock, sizeof mSizeLastBlock );
  offset += sizeof mSizeLastBlock;
  memset( buff + offset, 0, msSizeHeader - offset );

  if ( !( pFile->Write( 0, msSizeHeader, buff ).IsOK() ) ) {
    mValid = false;
  } else {
    mValid = true;
  }

  free( buff );
  return mValid;
}

EOSFSTNAMESPACE_END
