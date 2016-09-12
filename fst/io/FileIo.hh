//------------------------------------------------------------------------------
//! @file FileIo.hh
//! @author Elvin-Alin Sindrilaru <esindril@cern.ch>
//! @brief Abstract class modelling an IO plugin
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

#ifndef __EOSFST_FILEIO_HH__
#define __EOSFST_FILEIO_HH__

/*----------------------------------------------------------------------------*/
#include <string>
#include <list>
/*----------------------------------------------------------------------------*/
#include "common/Logging.hh"
#include "common/Statfs.hh"
#include "fst/Namespace.hh"
#include "fst/XrdFstOfsFile.hh"
/*----------------------------------------------------------------------------*/

//------------------------------------------------------------------------------
//! The truncate offset (1TB) is used to indicate that a file should be deleted
//! during the close as there is no better interface usable via XrdCl to
//! communicate a deletion on a open file
//------------------------------------------------------------------------------
#define EOS_FST_DELETE_FLAG_VIA_TRUNCATE_LEN 1024 * 1024 * 1024 * 1024ll
#define EOS_FST_NOCHECKSUM_FLAG_VIA_TRUNCATE_LEN ((1024 * 1024 * 1024 * 1024ll)+1)

EOSFSTNAMESPACE_BEGIN

class FileIo : public eos::common::LogId
{
public:

  //--------------------------------------------------------------------------
  //! Default constructor
  //--------------------------------------------------------------------------
  FileIo() {}

  //--------------------------------------------------------------------------
  //! Constructor with paramters
  //!
  //! @param path the path associated with this plugin instance
  //! @param ioType the type of this plugin instance
  //--------------------------------------------------------------------------
  FileIo(std::string path, std::string ioType) :
    eos::common::LogId(),
    mFilePath(path),
    mType(ioType),
    mLastUrl(""),
    mLastErrMsg(""),
    mIsOpen(false),
    mExternalStorage(false)
  {}

  //--------------------------------------------------------------------------
  //! Destructor
  //--------------------------------------------------------------------------
  virtual ~FileIo() {}

  //--------------------------------------------------------------------------
  //! Open file
  //!
  //! @param flags open flags
  //! @param mode open mode
  //! @param opaque opaque information
  //! @param timeout timeout value
  //! @return 0 if successful, -1 otherwise and error code is set
  //--------------------------------------------------------------------------
  virtual int fileOpen(XrdSfsFileOpenMode flags,
                       mode_t mode = 0,
                       const std::string& opaque = "",
                       uint16_t timeout = 0) = 0;

  //--------------------------------------------------------------------------
  //! Open file asynchronously
  //! @return 0 if successful, -1 otherwise and error code is set
  //--------------------------------------------------------------------------

  virtual int fileOpenAsync(void* io_handler,
                            XrdSfsFileOpenMode flags, mode_t mode = 0,
                            const std::string& opaque = "", uint16_t timeout = 0)
  {
    return -1;
  }

  //--------------------------------------------------------------------------
  //! Read from file - sync
  //!
  //! @param offset offset in file
  //! @param buffer where the data is read
  //! @param length read length
  //! @param timeout timeout value
  //! @return number of bytes read or -1 if error
  //--------------------------------------------------------------------------
  virtual int64_t fileRead(XrdSfsFileOffset offset,
                           char* buffer,
                           XrdSfsXferSize length,
                           uint16_t timeout = 0) = 0;

  //----------------------------------------------------------------------------
  //! Vector read - sync
  //!
  //! @param chunkList list of chunks for the vector read
  //! @param timeout timeout value
  //!
  //! @return number of bytes read of -1 if error
  //----------------------------------------------------------------------------
  virtual int64_t fileReadV(XrdCl::ChunkList& chunkList,
                            uint16_t timeout = 0) = 0;

  //------------------------------------------------------------------------------
  //! Vector read - async
  //!
  //! @param chunkList list of chunks for the vector read
  //! @param timeout timeout value
  //!
  //! @return 0(SFS_OK) if request successfully sent, otherwise -1(SFS_ERROR)
  //------------------------------------------------------------------------------
  virtual int64_t fileReadVAsync(XrdCl::ChunkList& chunkList,
                                 uint16_t timeout = 0) = 0;

  //----------------------------------------------------------------------------
  //! Write to file - sync
  //!
  //! @param offset offset
  //! @param buffer data to be written
  //! @param length length
  //! @param timeout timeout value
  //! @return number of bytes written or -1 if error
  //--------------------------------------------------------------------------
  virtual int64_t fileWrite(XrdSfsFileOffset offset,
                            const char* buffer,
                            XrdSfsXferSize length,
                            uint16_t timeout = 0) = 0;

  //----------------------------------------------------------------------------
  //! Read from file - async
  //!
  //! @param offset offset in file
  //! @param buffer where the data is read
  //! @param length read length
  //! @param readahead set if readahead is to be used
  //! @param timeout timeout value
  //! @return number of bytes read or -1 if error
  //--------------------------------------------------------------------------
  virtual int64_t fileReadAsync(XrdSfsFileOffset offset,
                                char* buffer,
                                XrdSfsXferSize length,
                                bool readahead = false,
                                uint16_t timeout = 0) = 0;

  //----------------------------------------------------------------------------
  //! Write to file - async
  //!
  //! @param offset offset
  //! @param buffer data to be written
  //! @param length length
  //! @param timeout timeout value
  //! @return number of bytes written or -1 if error
  //--------------------------------------------------------------------------
  virtual int64_t fileWriteAsync(XrdSfsFileOffset offset,
                                 const char* buffer,
                                 XrdSfsXferSize length,
                                 uint16_t timeout = 0) = 0;

  //----------------------------------------------------------------------------
  //! Truncate
  //!
  //! @param offset truncate file to this value
  //! @param timeout timeout value
  //! @return 0 if successful, -1 otherwise and error code is set
  //--------------------------------------------------------------------------
  virtual int fileTruncate(XrdSfsFileOffset offset, uint16_t timeout = 0) = 0;

  //----------------------------------------------------------------------------
  //! Allocate file space
  //!
  //! @param length space to be allocated
  //! @return 0 on success, -1 otherwise and error code is set
  //--------------------------------------------------------------------------
  virtual int fileFallocate(XrdSfsFileOffset length) = 0;

  //----------------------------------------------------------------------------
  //! Deallocate file space
  //!
  //! @param fromOffset offset start
  //! @param toOffset offset end
  //! @return 0 on success, -1 otherwise and error code is set
  //--------------------------------------------------------------------------
  virtual int fileFdeallocate(XrdSfsFileOffset fromOffset,
                              XrdSfsFileOffset toOffset) = 0;

  //----------------------------------------------------------------------------
  //! Remove file
  //!
  //! @param timeout timeout value
  //! @return 0 on success, -1 otherwise and error code is set
  //--------------------------------------------------------------------------
  virtual int fileRemove(uint16_t timeout = 0) = 0;

  //----------------------------------------------------------------------------
  //! Sync file to disk
  //!
  //! @param timeout timeout value
  //! @return 0 on success, -1 otherwise and error code is set
  //--------------------------------------------------------------------------
  virtual int fileSync(uint16_t timeout = 0) = 0;

  //--------------------------------------------------------------------------
  //! Get pointer to async meta handler object
  //!
  //! @return pointer to async handler, NULL otherwise
  //--------------------------------------------------------------------------
  virtual void* fileGetAsyncHandler() = 0;

  //--------------------------------------------------------------------------
  //! Check for the existence of a file
  //!
  //! @param path to the file
  //! @return 0 on success, -1 otherwise and error code is set
  //--------------------------------------------------------------------------
  virtual int fileExists() = 0;

  //----------------------------------------------------------------------------
  //! Close file
  //!
  //! @param timeout timeout value
  //! @return 0 on success, -1 otherwise and error code is set
  //--------------------------------------------------------------------------
  virtual int fileClose(uint16_t timeout = 0) = 0;

  //----------------------------------------------------------------------------
  //! Get stats about the file
  //!
  //! @param buf stat buffer
  //! @param timeout timeout value
  //! @return 0 on success, -1 otherwise and error code is set
  //--------------------------------------------------------------------------
  virtual int fileStat(struct stat* buf, uint16_t timeout = 0) = 0;


  // ------------------------------------------------------------------------
  //! Set a binary attribute (name has to start with 'user.' !!!)
  //!
  //! @param name attribute name
  //! @param value attribute value
  //! @param len value length
  //! @return 0 on success, -1 otherwise and error code is set
  // ------------------------------------------------------------------------
  virtual int attrSet(const char* name, const char* value, size_t len) = 0;

  // ------------------------------------------------------------------------
  //! Set a binary attribute (name has to start with 'user.' !!!)
  //!
  //! @param name attribute name
  //! @param value attribute value
  //! @return 0 on success, -1 otherwise and error code is set
  // ------------------------------------------------------------------------
  virtual int attrSet(string name, std::string value) = 0;

  // ------------------------------------------------------------------------
  //! Get a binary attribute by name
  //!
  //! @param name attribute name
  //! @param value contains attribute value upon success
  //! @param size the buffer size, after success the value size
  //! @return 0 on success, -1 otherwise and error code is set
  // ------------------------------------------------------------------------
  virtual int attrGet(const char* name, char* value, size_t& size) = 0;

  // ------------------------------------------------------------------------
  //! Get a binary attribute by name
  //!
  //! @param name attribute name
  //! @param value contains attribute value upon success
  //! @return 0 on success, -1 otherwise and error code is set
  // ------------------------------------------------------------------------
  virtual int attrGet(string name, std::string& value) = 0;

  // ------------------------------------------------------------------------
  //! Delete a binary attribute by name
  //!
  //! @param name attribute name
  //! @return 0 on success, -1 otherwise and error code is set
  // ------------------------------------------------------------------------
  virtual int attrDelete(const char* name) = 0;

  // ------------------------------------------------------------------------
  //! List all attributes for the associated path
  //!
  //! @param list contains all attribute names for the set path upon success
  //! @return 0 on success, -1 otherwise and error code is set
  // ------------------------------------------------------------------------
  virtual int attrList(std::vector<std::string>& list) = 0;

  class FtsHandle
  {
  public:
    FtsHandle(const char* dirp) : mPath(dirp) {}
    virtual ~FtsHandle() {}

  protected:
    std::string mPath;
  };

  //--------------------------------------------------------------------------
  //! Open a cursor to traverse a storage system
  //! @param subtree where to start traversing
  //! @return returns implementation dependent handle or 0 in case of error
  //--------------------------------------------------------------------------
  virtual FileIo::FtsHandle* ftsOpen() = 0;

  //----------------------------------------------------------------------------
  //! Get pointer to async meta handler object
  //!
  //! @param fts_handle cursor obtained by ftsOpen
  //! @return returns implementation dependent handle or 0 in case of error
  //--------------------------------------------------------------------------
  virtual std::string ftsRead(FtsHandle* handle) = 0;

  //--------------------------------------------------------------------------
  //! Close a traversal cursor
  //!
  //! @param fts_handle cursor to close
  //! @return 0 if fts_handle was an open cursor, otherwise -1
  //--------------------------------------------------------------------------
  virtual int ftsClose(FtsHandle* handle) = 0;

  //--------------------------------------------------------------------------
  //! Plug-in function to fill a statfs structure about the storage filling
  //! state
  //! @param path to statfs
  //! @param statfs return struct
  //! @return 0 if successful otherwise errno
  //--------------------------------------------------------------------------
  virtual int Statfs(struct statfs* statFs) = 0;

  //--------------------------------------------------------------------------
  //! Callback function to fill a statfs structure about the storage filling
  //! state
  //! @param data containing path, return code and statfs structure
  //! @return 0 if successful otherwise errno
  //--------------------------------------------------------------------------
  static int StatfsCB(eos::common::Statfs::Callback::callback_data* data)
  {
    if (data && data->caller) {
      data->retc = ((FileIo*)(data->caller))->Statfs(data->statfs);
      return data->retc;
    } else {
      return -1;
    }
  }

  // -------------------------------------------------------------------------
  // Mark this IO as an IO module towards an external storage system
  // -------------------------------------------------------------------------
  void SetExternalStorage()
  {
    mExternalStorage = true;
  }

  // -------------------------------------------------------------------------
  // Return the IO type
  // -------------------------------------------------------------------------
  std::string GetIoType()
  {
    return mType;
  }

  std::string GetLastUrl()
  {
    return mLastUrl;
  }

  std::string GetPath()
  {
    return mFilePath;
  }

  //--------------------------------------------------------------------------
  //! Get last error message
  //--------------------------------------------------------------------------
  const std::string& GetLastErrMsg()
  {
    return mLastErrMsg;
  }


  //--------------------------------------------------------------------------
  //! Get last error code
  //--------------------------------------------------------------------------
  const int&
  GetLastErrCode()
  {
    return mLastErrCode;
  }

  //--------------------------------------------------------------------------
  //! Get last error number
  //--------------------------------------------------------------------------
  const int&
  GetLastErrNo()
  {
    return mLastErrNo;
  }

protected:
  const std::string mFilePath; ///< path to current physical file
  const std::string mType; ///< type
  std::string mLastUrl; ///< last used url if remote file
  std::string mLastErrMsg; ///< last error message seen
  int mLastErrCode; ///< last error code
  int mLastErrNo; ///< last error no
  bool mIsOpen; ///< Mark if file is opened, so that we close it properly
  bool mExternalStorage; ///< indicates if this is an IO module to talk to an external storage system
};

EOSFSTNAMESPACE_END

#endif  // __EOSFST_FILEIO_HH__
