// ----------------------------------------------------------------------
// File: proc/user/Rm.cc
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
#include "mgm/ProcInterface.hh"
#include "mgm/XrdMgmOfs.hh"
#include "mgm/Access.hh"

/*----------------------------------------------------------------------------*/

EOSMGMNAMESPACE_BEGIN

int
ProcCommand::Rm ()
{
 XrdOucString spath = pOpaque->Get("mgm.path");
 XrdOucString option = pOpaque->Get("mgm.option");
 XrdOucString deep = pOpaque->Get("mgm.deletion");

 const char* inpath = spath.c_str();
 eos::common::Path cPath(inpath);

 NAMESPACEMAP;
 info = 0;
 if (info)info = 0; // for compiler happyness
 PROC_BOUNCE_ILLEGAL_NAMES;
 PROC_BOUNCE_NOT_ALLOWED;

 spath = path;

 if (!spath.length())
 {
   stdErr = "error: you have to give a path name to call 'rm'";
   retc = EINVAL;
 }
 else
 {
   // find everything to be deleted
   if (option == "r")
   {
     std::map<std::string, std::set<std::string> > found;
     std::map<std::string, std::set<std::string> >::const_reverse_iterator rfoundit;
     std::set<std::string>::const_iterator fileit;

     if (((cPath.GetSubPathSize() < 4) && (deep != "deep")) || (gOFS->_find(spath.c_str(), *error, stdErr, *pVid, found)))
     {
       if ((cPath.GetSubPathSize() < 4) && (deep != "deep"))
       {
         stdErr += "error: deep recursive deletes are forbidden without shell confirmation code!";
         retc = EPERM;
       }
       else
       {
         stdErr += "error: unable to remove file/directory";
         retc = errno;
       }
     }
     else
     {
       // delete files starting at the deepest level
       for (rfoundit = found.rbegin(); rfoundit != found.rend(); rfoundit++)
       {
         for (fileit = rfoundit->second.begin(); fileit != rfoundit->second.end(); fileit++)
         {
           std::string fspath = rfoundit->first;
           fspath += *fileit;
           if (gOFS->_rem(fspath.c_str(), *error, *pVid, (const char*) 0))
           {
             stdErr += "error: unable to remove file\n";
             retc = errno;
           }
         }
       }
       // delete directories starting at the deepest level
       for (rfoundit = found.rbegin(); rfoundit != found.rend(); rfoundit++)
       {
         // don't even try to delete the root directory
         std::string fspath = rfoundit->first.c_str();
         if (fspath == "/")
           continue;
         if (gOFS->_remdir(rfoundit->first.c_str(), *error, *pVid, (const char*) 0))
         {
           stdErr += "error: unable to remove directory";
           retc = errno;
         }
       }
     }
   }
   else
   {
     if (gOFS->_rem(spath.c_str(), *error, *pVid, (const char*) 0))
     {
       stdErr += "error: unable to remove file/directory";
       retc = errno;
     }
   }
 }
 return SFS_OK;
}

EOSMGMNAMESPACE_END
