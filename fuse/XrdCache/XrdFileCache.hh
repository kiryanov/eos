//------------------------------------------------------------------------------
// File: XrdFileCache.hh
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

#ifndef __EOS_XRDFILECACHE_HH__
#define __EOS_XRDFILECACHE_HH__

//------------------------------------------------------------------------------
#include <pthread.h>
//------------------------------------------------------------------------------
#include "ConcurrentQueue.hh"
#include "FileAbstraction.hh"
#include "CacheImpl.hh"
#include "CacheEntry.hh"
//------------------------------------------------------------------------------

class CacheImpl;

//------------------------------------------------------------------------------
//! Class implementing the high-level constructs needed to operate the caching
//! framenwork
//------------------------------------------------------------------------------
class XrdFileCache
{
 public:

    // -------------------------------------------------------------------------
    //! Get instance of class
    //!
    //! @param s_max maximum size
    //!
    // -------------------------------------------------------------------------
    static XrdFileCache* GetInstance( size_t s_max );

    // -------------------------------------------------------------------------
    //! Destructor
    // -------------------------------------------------------------------------
    ~XrdFileCache();

    // -------------------------------------------------------------------------
    //! Add a write request
    //!
    //! @param ref_file XrdCl file handler
    //! @param inode file inode value
    //! @param buf data to be written
    //! @param off offset
    //! @param len length
    //!
    // -------------------------------------------------------------------------
    void SubmitWrite( XrdCl::File*& ref_file,
                      unsigned long inode,
                      void*         buf,
                      off_t         off,
                      size_t        len );

    // -------------------------------------------------------------------------
    //! Try to get read from cache
    //!
    //! @param fAbst FileAbstraction handler
    //! @param buf buffer where to read the data
    //! @param off offset
    //! @param len length
    //!
    //! @return number of bytes read
    //!
    // -------------------------------------------------------------------------
    size_t GetRead( FileAbstraction& fAbst, void* buf, off_t off, size_t len );

    // -------------------------------------------------------------------------
    //! Add read to cache
    //!
    //! @param ref_file XrdCl file handler
    //! @param fAbst FileAbstraction handler
    //! @param buf buffer containing the data
    //! @param off offset
    //! @param len length
    //!
    //! @return number of bytes saved in cache
    //!
    // -------------------------------------------------------------------------
    size_t PutRead( XrdCl::File*&    ref_file,
                    FileAbstraction& fAbst,
                    void*            buf,
                    off_t            off,
                    size_t           len );

    // -------------------------------------------------------------------------
    //! Wait for all pending writes on a file
    //!
    //! @param inode file inode
    //!
    // -------------------------------------------------------------------------
    void WaitFinishWrites( unsigned long inode );

    // -------------------------------------------------------------------------
    //! Wait for all pending writes on a file
    //!
    //! @param fAbst FileAbstraction handler
    //!
    // -------------------------------------------------------------------------
    void WaitFinishWrites( FileAbstraction& fAbst );

    // -------------------------------------------------------------------------
    //! Remove file inode from mapping. If strongConstraint is true then we
    //! impose tighter constraints on when we consider a file as not beeing
    //! used (for the strong case the file has to have  no read or write blocks
    //! in cache and the number of references to held to it has to be 0).
    //!
    //! @param inode file inode
    //! @param strong_constraint enforce tighter constraints
    //!
    //! @return true if file obj was removed, otherwise false
    //!
    // -------------------------------------------------------------------------
    bool RemoveFileInode( unsigned long inode, bool strong_constraint );

    // -------------------------------------------------------------------------
    //! Get handler to the errors queue
    //!
    //! @param inode file inode
    //!
    //! @return error queue
    //!
    // -------------------------------------------------------------------------
    ConcurrentQueue<error_type>& GetErrorQueue( unsigned long inode );

    // -------------------------------------------------------------------------
    //! Get handler to the file abstraction object
    //!
    //! @param inode file inode
    //! @param get_new if true then force creation of a new object
    //!
    //! @return FileAbstraction handler
    //!
    // -------------------------------------------------------------------------
    FileAbstraction* GetFileObj( unsigned long inode, bool get_new );

    //vector reads - no implemented
    /*size_t getReadV(unsigned long inode,
                      int           filed,
                      void*         buf,
                      off_t*        offset,
                      size_t*       length,
                      int           nbuf);

    void putReadV(unsigned long inode,
                  int           filed,
                  void*         buf,
                  off_t*        offset,
                  size_t*       length,
                  int           nbuf);
    */

  private:

    //< maximum number of files concurrently in cache, has to be >=10
    static const int max_index_files = 1000;

    //< singleton object
    static XrdFileCache* pInstance;

    // -------------------------------------------------------------------------
    //! Constructor
    //!
    //! @param s_max maximum size
    //!
    // -------------------------------------------------------------------------
    XrdFileCache( size_t s_max );

    // -------------------------------------------------------------------------
    //! Initialization method
    // -------------------------------------------------------------------------
    void Init();

    // -------------------------------------------------------------------------
    //! Method ran by the asynchronous thread doing writes
    // -------------------------------------------------------------------------
    static void* WriteThreadProc( void* );

    size_t cache_size_max;    //< read cache size
    int indexFile;            //< last index assigned to a file

    pthread_t write_thread;   //< async thread doing the writes
    XrdSysRWLock rw_lock;     //< rw lock for the key map

    //< file indices used and available to recycle
    ConcurrentQueue<int>* queue_used_indx;

    //< map inodes to FileAbst objects
    std::map<unsigned long, FileAbstraction*> inode2fAbst;

    CacheImpl* cache_impl;    //< handler to the low-level cache implementation
};

#endif
