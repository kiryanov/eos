// ----------------------------------------------------------------------
// File: ProcInterface.cc
// Author: Andreas-Joachim Peters - CERN
// ----------------------------------------------------------------------

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
 * You should have received a copy of the AGNU General Public License    *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.*
 ************************************************************************/

/*----------------------------------------------------------------------------*/
#include "common/FileId.hh"
#include "common/LayoutId.hh"
#include "common/Mapping.hh"
#include "common/StringConversion.hh"
#include "common/Path.hh"
#include "mgm/Acl.hh"
#include "mgm/Access.hh"
#include "mgm/FileSystem.hh"
#include "mgm/Policy.hh"
#include "mgm/ProcInterface.hh"
#include "mgm/XrdMgmOfs.hh"
#include "mgm/FsView.hh"
#include "mgm/Master.hh"
#include "namespace/persistency/LogManager.hh"
#include "namespace/utils/DataHelper.hh"
#include "namespace/views/HierarchicalView.hh"
#include "namespace/persistency/ChangeLogContainerMDSvc.hh"
#include "namespace/persistency/ChangeLogFileMDSvc.hh"
/*----------------------------------------------------------------------------*/
#include "XrdSfs/XrdSfsInterface.hh"
/*----------------------------------------------------------------------------*/
#include <iostream>
#include <fstream>
/*----------------------------------------------------------------------------*/

#include <vector>
#include <map>
#include <string>
#include <math.h>

EOSMGMNAMESPACE_BEGIN

/*----------------------------------------------------------------------------*/
/**
 * Constructor 
 */
ProcInterface::ProcInterface () { }

/*----------------------------------------------------------------------------*/
/**
 * Destructor 
 */
/*----------------------------------------------------------------------------*/
ProcInterface::~ProcInterface () { }

/*----------------------------------------------------------------------------*/
/**
 * Check if a path indicates a proc command
 * @param path input path for a proc command
 * @return true if proc command otherwise false
 */
/*----------------------------------------------------------------------------*/
bool
ProcInterface::IsProcAccess (const char* path)
{
 XrdOucString inpath = path;
 if (inpath.beginswith("/proc/"))
 {
   return true;
 }
 return false;
}

/**
 * Check if a proc command is a 'write' command modifying state of an MGM
 * @param path input arguments for proc command
 * @param info CGI for proc command
 * @return true if write access otherwise false
 */
/*----------------------------------------------------------------------------*/
bool
ProcInterface::IsWriteAccess (const char* path, const char* info)
{
 XrdOucString inpath = (path ? path : "");
 XrdOucString ininfo = (info ? info : "");

 if (!inpath.beginswith("/proc/"))
 {
   return false;
 }

 XrdOucEnv procEnv(ininfo.c_str());
 XrdOucString cmd = procEnv.Get("mgm.cmd");
 XrdOucString subcmd = procEnv.Get("mgm.subcmd");

 // ----------------------------------------------------------------------------
 // filter here all namespace modifying proc messages
 // ----------------------------------------------------------------------------
 if (((cmd == "file") &&
      ((subcmd == "adjustreplica") ||
       (subcmd == "drop") ||
       (subcmd == "layout") ||
       (subcmd == "verify") ||
       (subcmd == "rename"))) ||
     ((cmd == "attr") &&
      ((subcmd == "set") ||
       (subcmd == "rm"))) ||
     ((cmd == "mkdir")) ||
     ((cmd == "rmdir")) ||
     ((cmd == "rm")) ||
     ((cmd == "chown")) ||
     ((cmd == "chmod")) ||
     ((cmd == "fs") &&
      ((subcmd == "config") ||
       (subcmd == "boot") ||
       (subcmd == "dropfiles") ||
       (subcmd == "add") ||
       (subcmd == "mv") ||
       (subcmd == "rm"))) ||
     ((cmd == "space") &&
      ((subcmd == "config") ||
       (subcmd == "define") ||
       (subcmd == "set") ||
       (subcmd == "rm") ||
       (subcmd == "quota"))) ||
     ((cmd == "node") &&
      ((subcmd == "rm") ||
       (subcmd == "config") ||
       (subcmd == "set") ||
       (subcmd == "register") ||
       (subcmd == "gw"))) ||
     ((cmd == "group") &&
      ((subcmd == "set") ||
       (subcmd == "rm"))) ||
     ((cmd == "map") &&
      ((subcmd == "link") ||
       (subcmd == "unlink"))) ||
     ((cmd == "quota") &&
      ((subcmd != "ls"))) ||
     ((cmd == "vid") &&
      ((subcmd != "ls"))) ||
     ((cmd == "transfer")))
 {

   return true;
 }

 return false;
}

/*----------------------------------------------------------------------------*/
/**
 * Authorize a proc command based on the clients VID
 * @param path specifies user or admin command path
 * @param info CGI providing proc arguments
 * @param vid virtual id of the client
 * @param entity security entity object
 * @return true if authorized otherwise false
 */
/*----------------------------------------------------------------------------*/
bool
ProcInterface::Authorize (const char* path,
                          const char* info,
                          eos::common::Mapping::VirtualIdentity& vid,
                          const XrdSecEntity* entity)
{
 XrdOucString inpath = path;

 // ----------------------------------------------------------------------------
 // administrator access
 // ----------------------------------------------------------------------------
 if (inpath.beginswith("/proc/admin/"))
 {
   // hosts with 'sss' authentication can run 'admin' commands
   std::string protocol = entity ? entity->prot : "";
   // we allow sss only with the daemon login is admin
   if ((protocol == "sss") && (eos::common::Mapping::HasUid(2, vid.uid_list)))
   {
     return true;
   }

   // root can do it
   if (!vid.uid)
   {
     return true;
   }
 
   // --------------------------------------------------------------------------
   // one has to be part of the virtual users 2(daemon) || 3(adm)/4(adm) 
   // --------------------------------------------------------------------------
   return ( (eos::common::Mapping::HasUid(2, vid.uid_list)) ||
           (eos::common::Mapping::HasUid(3, vid.uid_list)) ||
           (eos::common::Mapping::HasGid(4, vid.gid_list)));
 }

 // ----------------------------------------------------------------------------
 // user access
 // ----------------------------------------------------------------------------
 if (inpath.beginswith("/proc/user/"))
 {
   return true;
 }
 
 return false;
}

/*----------------------------------------------------------------------------*/
/**
 * Constructor ProcCommand
 */
/*----------------------------------------------------------------------------*/
ProcCommand::ProcCommand ()
{
 stdOut = "";
 stdErr = "";
 stdJson = "";
 retc = 0;
 mResultStream = "";
 mOffset = 0;
 mLen = 0;
 pVid = 0;
 path = "";
 mAdminCmd = mUserCmd = 0;
 mError = 0;
 mComment = "";
 mArgs = "";
 mExecTime = time(NULL);
 mClosed = true;
 pOpaque = 0;
 ininfo = 0;
 fstdout = fstderr = fresultStream = 0;
 fstdoutfilename = fstderrfilename = fresultStreamfilename = "";
}

/*----------------------------------------------------------------------------*/
/**
 * Destructor
 */
/*----------------------------------------------------------------------------*/
ProcCommand::~ProcCommand ()
{
 if (fstdout)
 {
   fclose(fstdout);
   fstdout = 0;
   unlink(fstdoutfilename.c_str());
 }

 if (fstderr)
 {
   fclose(fstderr);
   fstderr = 0;
   unlink(fstderrfilename.c_str());
 }

 if (fresultStream)
 {
   fclose(fresultStream);
   fresultStream = 0;
   unlink(fresultStreamfilename.c_str());
 }

 if (pOpaque)
 {
   delete pOpaque;
   pOpaque = 0;
 }
}

/*----------------------------------------------------------------------------*/
/**
 * Open temporary output files for results of find commands
 * @return true if successful otherwise false
 */
/*----------------------------------------------------------------------------*/
bool
ProcCommand::OpenTemporaryOutputFiles ()
{
 char tmpdir [4096];
 snprintf(tmpdir, sizeof (tmpdir) - 1, "/tmp/eos.mgm/%llu", (unsigned long long) XrdSysThread::ID());
 fstdoutfilename = tmpdir;
 fstdoutfilename += ".stdout";
 fstderrfilename = tmpdir;
 fstderrfilename += ".stderr";
 fresultStreamfilename = tmpdir;
 fresultStreamfilename += ".mResultstream";

 eos::common::Path cPath(fstdoutfilename.c_str());

 if (!cPath.MakeParentPath(S_IRWXU))
 {
   eos_err("Unable to create temporary outputfile directory %s", tmpdir);
   return false;
 }

 // own the directory by daemon
 if (::chown(cPath.GetParentPath(), 2, 2))
 {
   eos_err("Unable to own temporary outputfile directory %s", cPath.GetParentPath());
 }

 fstdout = fopen(fstdoutfilename.c_str(), "w");
 fstderr = fopen(fstderrfilename.c_str(), "w");
 fresultStream = fopen(fresultStreamfilename.c_str(), "w+");

 if ((!fstdout) || (!fstderr) || (!fresultStream))
 {
   if (fstdout) fclose(fstdout);
   if (fstderr) fclose(fstderr);
   if (fresultStream) fclose(fresultStream);
   return false;
 }
 return true;
}

/*----------------------------------------------------------------------------*/
/**
 * open a proc command e.g. call the appropriate user or admin commmand and
 * store the output in a resultstream of in case of find in temporary output
 * files.
 * @param inpath path indicating user or admin command
 * @param info CGI describing the proc command
 * @param vid_in virtual identity of the user requesting a command
 * @param error object to store errors
 * @return SFS_OK in any case
 */
/*----------------------------------------------------------------------------*/
int
ProcCommand::open (const char* inpath, const char* info, eos::common::Mapping::VirtualIdentity &vid_in, XrdOucErrInfo *error)
{
 pVid = &vid_in;
 mClosed = false;
 path = inpath;
 mDoSort = false;
 mError = error;

 ininfo = info;
 if ((path.beginswith("/proc/admin")))
 {
   mAdminCmd = true;
 }
 if (path.beginswith("/proc/user"))
 {
   mUserCmd = true;
 }

 pOpaque = new XrdOucEnv(ininfo);

 if (!pOpaque)
 {
   // alloc failed 
   return SFS_ERROR;
 }

 mOutFormat = "";
 mCmd = pOpaque->Get("mgm.cmd");
 mSubCmd = pOpaque->Get("mgm.subcmd");
 mOutFormat = pOpaque->Get("mgm.outformat");
 mSelection = pOpaque->Get("mgm.selection");
 mComment = pOpaque->Get("mgm.mComment") ? pOpaque->Get("mgm.mComment") : "";
 int envlen = 0;
 mArgs = pOpaque->Env(envlen);

 mFuseFormat = false;
 mJsonFormat = false;
 
 // ----------------------------------------------------------------------------
 // if set to FUSE, don't print the stdout,stderr tags and we guarantee a line 
 // feed in the end
 // ----------------------------------------------------------------------------
 
 XrdOucString format = pOpaque->Get("mgm.format"); 

 if (format == "fuse")
 {
   mFuseFormat = true;
 }
 if (format == "json")
 {
   mJsonFormat = true;
 }

 stdOut = "";
 stdErr = "";
 retc = 0;
 mResultStream = "";
 mOffset = 0;
 mLen = 0;
 mDoSort = true;

 // ----------------------------------------------------------------------------
 // admin command section
 // ----------------------------------------------------------------------------
 if (mAdminCmd)
 {
   if (mCmd == "access")
   {
     Access();
   }
   else

     if (mCmd == "config")
   {
     Config();
   }
   else

     if (mCmd == "node")
   {
     Node();
   }
   else
     if (mCmd == "space")
   {
     Space();
   }
   else

     if (mCmd == "group")
   {
     Group();
   }
   else
     if (mCmd == "fs")
   {
     Fs();
   }
   else
     if (mCmd == "ns")
   {
     Ns();
   }
   else
     if (mCmd == "io")
   {
     Io();
   }
   else
     if (mCmd == "fsck")
   {
     Fsck();
   }
   else
     if (mCmd == "quota")
   {
     Quota();
   }
   else
     if (mCmd == "transfer")
   {
     Transfer();
     mDoSort = false;
   }
   else
     if (mCmd == "debug")
   {
     Debug();
   }
   else
     if (mCmd == "vid")
   {
     Vid();
   }
   else
     if (mCmd == "rtlog")
   {
     Rtlog();
   }
   else
     if (mCmd == "chown")
   {
     Chown();
   }
   else
   {
     // command is not implemented
     stdErr += "errro: no such admin command '";
     stdErr += mCmd;
     stdErr += "'";
     retc = EINVAL;
   }

   MakeResult();
   return SFS_OK;
 }

 // ----------------------------------------------------------------------------
 // user command section
 // ----------------------------------------------------------------------------
 if (mUserCmd)
 {
   if (mCmd == "motd")
   {
     Motd();
     mDoSort = false;
   }
   else
     if (mCmd == "version")
   {
     Version();
     mDoSort = false;
   }
   else
     if (mCmd == "quota")
   {
     Quota();
     mDoSort = false;
   }
   else
     if (mCmd == "who")
   {
     Who();
     mDoSort = false;
   }
   else
     if (mCmd == "fuse")
   {
     return Fuse();
   }
   else
     if (mCmd == "file")
   {
     File();
     mDoSort = false;
   }
   else
     if (mCmd == "fileinfo")
   {
     Fileinfo();
     mDoSort = false;
   }
   else
     if (mCmd == "mkdir")
   {
     Mkdir();
   }
   else
     if (mCmd == "rmdir")
   {
     Rmdir();
   }
   else
     if (mCmd == "cd")
   {
     Cd();
     mDoSort = false;
   }
   else
     if (mCmd == "ls")
   {
     Ls();
     mDoSort = false;
   }
   else
     if (mCmd == "rm")
   {
     Rm();
   }
   else
     if (mCmd == "whoami")
   {
     Whoami();
     mDoSort = false;
   }
   else
     if (mCmd == "find")
   {
     Find();
   }
   else
     if (mCmd == "map")
   {
     Map();
   }
   else
     if (mCmd == "attr")
   {
     Attr();
   }
   else
     if (mCmd == "chmod")
   {
     if (Chmod() == SFS_OK) return SFS_OK;
   }
   else
   { 
     // ------------------------------------------------------------------------
     // command not implemented
     // ------------------------------------------------------------------------
     stdErr += "errro: no such user command '";
     stdErr += mCmd;
     stdErr += "'";
     retc = EINVAL;
   }
   MakeResult();
   return SFS_OK;
 }

 // ----------------------------------------------------------------------------
 // if neither admin nor proc command
 // ----------------------------------------------------------------------------
 return gOFS->Emsg((const char*) "open", *error, EINVAL, "execute command - not implemented ", ininfo);
}

/*----------------------------------------------------------------------------*/
/**
 * read a part of the result stream produced during open
 * @param mOffset offset where to start
 * @param buff buffer to store stream
 * @param blen len to return
 * @return number of bytes read
 */
/*----------------------------------------------------------------------------*/
int
ProcCommand::read (XrdSfsFileOffset mOffset, char* buff, XrdSfsXferSize blen)
{
 if (fresultStream)
 {
   // file based results go here ...
   if ((fseek(fresultStream, mOffset, 0)) == 0)
   {
     size_t nread = fread(buff, 1, blen, fresultStream);
     if (nread > 0)
       return nread;
   }
   else
   {
     eos_err("seek to %llu failed\n", mOffset);
   }
   return 0;
 }
 else
 {
   // memory based results go here ...
   if (((unsigned int) blen <= (mLen - mOffset)))
   {
     memcpy(buff, mResultStream.c_str() + mOffset, blen);
     return blen;
   }
   else
   {
     memcpy(buff, mResultStream.c_str() + mOffset, (mLen - mOffset));
     return (mLen - mOffset);
   }
 }
}

/*----------------------------------------------------------------------------*/
/**
 * return stat information for the result stream to tell the client the size
 * of the proc output
 * @param buf stat structure to fill
 * @return SFS_OK in any case
 */
/*----------------------------------------------------------------------------*/
int
ProcCommand::stat (struct stat* buf)
{
 memset(buf, 0, sizeof (struct stat));
 buf->st_size = mLen;

 return SFS_OK;
}

/*----------------------------------------------------------------------------*/
/**
 * close the proc stream and store the clients comment for the command in the
 * comment log file
 * @return 0 if comment has been successfully stored otherwise !=0
 */
int
ProcCommand::close ()
{
 if (!mClosed)
 {
   // only instance users or sudoers can add to the log book
   if ((pVid->uid <= 2) || (pVid->sudoer))
   {
     if (mComment.length() && gOFS->commentLog)
     {
       if (!gOFS->commentLog->Add(mExecTime, mCmd.c_str(), mSubCmd.c_str(), mArgs.c_str(), mComment.c_str(), stdErr.c_str(), retc))
       {
         eos_err("failed to log to comment log file");
       }
     }
   }
   mClosed = true;
 }
 return retc;
}

/*----------------------------------------------------------------------------*/
/**
 * Build the inmemory result of the stdout,stderr & retc of the proc commdn
 * Depending on the output format the key-value CGI returned changes => see
 * implementation.
 */
/*----------------------------------------------------------------------------*/
void
ProcCommand::MakeResult ()
{
 mResultStream = "";

 if (!fstdout)
 {
   XrdMqMessage::Sort(stdOut, mDoSort);
   if ((!mFuseFormat && !mJsonFormat))
   {
     // ------------------------------------------------------------------------
     // the default format
     // ------------------------------------------------------------------------
     mResultStream = "mgm.proc.stdout=";
     mResultStream += XrdMqMessage::Seal(stdOut);
     mResultStream += "&mgm.proc.stderr=";
     mResultStream += XrdMqMessage::Seal(stdErr);
     mResultStream += "&mgm.proc.retc=";
     mResultStream += retc;

   }
   if (mFuseFormat)
   {
     // ------------------------------------------------------------------------
     // FUSE format contains only STDOUT
     // ------------------------------------------------------------------------
     mResultStream += stdOut;
   }
   if (mJsonFormat)
   {
     // ------------------------------------------------------------------------
     // only few commands actually return stdJson as output
     // ------------------------------------------------------------------------
     if (!stdJson.length())
     {
       stdJson = "{\n  \"error\": \"command does not provide JSON output\",\n  \"errc\": 93\n}";
     }
     mResultStream = "mgm.proc.json=";
     mResultStream += stdJson;
   }
   if (!mResultStream.endswith('\n'))
   {
     mResultStream += "\n";
   }
   if (retc)
   {
     eos_static_err("%s (errno=%u)", stdErr.c_str(), retc);
   }
   mLen = mResultStream.length();
   mOffset = 0;
 }
 else
 {
   // --------------------------------------------------------------------------
   // file based results CANNOT be sorted and don't have mFuseFormat
   // --------------------------------------------------------------------------
   if (!mFuseFormat)
   {
     // ------------------------------------------------------------------------
     // create the stdout result
     // ------------------------------------------------------------------------
     if (!fseek(fstdout, 0, 0) && 
         !fseek(fstderr, 0, 0) && 
         !fseek(fresultStream, 0, 0))
     {
       fprintf(fresultStream, "&mgm.proc.stdout=");

       std::ifstream inStdout(fstdoutfilename.c_str());
       std::ifstream inStderr(fstderrfilename.c_str());
       std::string entry;

       while (std::getline(inStdout, entry))
       {
         XrdOucString sentry = entry.c_str();
         sentry += "\n";
         if (!mFuseFormat)
         {
           XrdMqMessage::Seal(sentry);
         }
         fprintf(fresultStream, "%s", sentry.c_str());
       }
       // ----------------------------------------------------------------------
       // close and remove - if this fails there is nothing to recover anyway
       // ----------------------------------------------------------------------
       fclose(fstdout);
       fstdout = 0;
       unlink(fstdoutfilename.c_str());
       // ----------------------------------------------------------------------
       // create the stderr result
       // ----------------------------------------------------------------------
       fprintf(fresultStream, "&mgm.proc.stderr=");
       while (std::getline(inStdout, entry))
       {
         XrdOucString sentry = entry.c_str();
         sentry += "\n";
         XrdMqMessage::Seal(sentry);
         fprintf(fresultStream, "%s", sentry.c_str());
       }
       // ----------------------------------------------------------------------
       // close and remove - if this fails there is nothing to recover anyway
       // ----------------------------------------------------------------------
       fclose(fstderr);
       fstderr = 0;
       unlink(fstderrfilename.c_str());

       fprintf(fresultStream, "&mgm.proc.retc=%d", retc);
       mLen = ftell(fresultStream);
       // ----------------------------------------------------------------------
       // spool the resultstream to the beginning
       // ----------------------------------------------------------------------
       fseek(fresultStream, 0, 0);
     }
     else
     {
       eos_static_err("cannot seek to position 0 in result files");
     }
   }
 }
}

/*----------------------------------------------------------------------------*/

EOSMGMNAMESPACE_END

