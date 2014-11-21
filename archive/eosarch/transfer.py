# ------------------------------------------------------------------------------
# File: tranfer.py
# Author: Elvin-Alin Sindrilaru <esindril@cern.ch>
# ------------------------------------------------------------------------------
#
# ******************************************************************************
# EOS - the CERN Disk Storage System
# Copyright (C) 2014 CERN/Switzerland
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
# ******************************************************************************
"""Module responsible for executing the archive transfer."""

import os
import time
import logging
import threading
import zmq
import ast
from os.path import join
from hashlib import sha256
from XRootD import client
from XRootD.client.flags import PrepareFlags, QueryCode
from eosarch.archivefile import ArchiveFile
from eosarch.utils import exec_cmd
from eosarch.asynchandler import MetaHandler
from eosarch.exceptions import NoErrorException


class ThreadJob(threading.Thread):
    """ Job executing a client.CopyProcess in a separate thread. This makes sense
    since a third-party copy job is mostly waiting for the completion of the
    job not doing any other operations and therefore not using the GIL too much.

    Attributes:
        status (bool): Final status of the job
        proc (client.CopyProcess): Copy process which is being executed
    """
    def __init__(self, cpy_proc):
        """Constructor

        Args:
            cpy_proc (client.CopyProcess): Copy process object
        """
        threading.Thread.__init__(self)
        self.status = None
        self.proc = cpy_proc
        self.xrd_status = None

    def run(self):
        """ Run method
        """
        self.xrd_status = self.proc.prepare()

        if self.xrd_status.ok:
            self.xrd_status = self.proc.run()


class ThreadStatus(threading.Thread):
    """ Thread responsible for replying to any requests comming from the
    dispatcher process.
    """
    def __init__(self, transfer):
        """ Constructor

        Args:
            transfer (Transfer): Current transfer object
        """
        threading.Thread.__init__(self)
        # TODO: drop the logger as it may interfere with the main thread
        self.logger = logging.getLogger("transfer")
        self.transfer = transfer
        self.run_status = True
        self.lock = threading.Lock()

    def run(self):
        """ Run method
        """
        self.logger.info("Starting the status thread")
        ctx = zmq.Context()
        socket_rr = ctx.socket(zmq.DEALER)
        socket_rr.connect("ipc://" + self.transfer.config.BACKEND_REQ_IPC)
        socket_ps = ctx.socket(zmq.SUB)
        mgr_filter = "[MASTER]"

        if isinstance(mgr_filter, bytes):
            mgr_filter = mgr_filter.encode('ascii')

        socket_ps.connect("ipc://" + self.transfer.config.BACKEND_PUB_IPC)
        socket_ps.setsockopt(zmq.SUBSCRIBE, mgr_filter)

        while self.keep_running():
            if socket_ps.poll(5000):
                try:
                    [__, msg] = socket_ps.recv_multipart()
                except zmq.ZMQError as err:
                    if err.errno == zmq.ETERM:
                        self.logger.error("ETERM error")
                        break # shutting down, exit
                    else:
                        self.logger.exception(err)
                        continue
                except Exception as err:
                    self.logger.exception(err)

                self.logger.debug("RECV_MSG: {0}".format(msg))
                dict_cmd = ast.literal_eval(msg)

                if dict_cmd['cmd'] == 'orphan_status':
                    self.logger.info("Reconnect to master ... ")
                    resp = ("{{'uuid': '{0}', "
                            "'pid': '{1}', "
                            "'uid': '{2}',"
                            "'gid': '{3}',"
                            "'root_dir': '{4}', "
                            "'op': '{5}',"
                            "'status': '{6}', "
                            "'timestamp': '{7}'"
                            "}}").format(self.transfer.uuid,
                                         self.transfer.pid,
                                         self.transfer.uid,
                                         self.transfer.gid,
                                         self.transfer.root_dir,
                                         self.transfer.oper,
                                         self.transfer.get_status(),
                                         self.transfer.timestamp)
                elif dict_cmd['cmd'] == 'status':
                    resp = ("{{'uuid': '{0}', "
                            "'status': '{1}'"
                            "}}").format(self.transfer.uuid,
                                         self.transfer.get_status())
                else:
                    self.logger.error("Unknown command: {0}".format(dict_cmd))
                    continue

                self.logger.info("Sending response: {0}".format(resp))
                socket_rr.send_multipart([resp], zmq.NOBLOCK)

    def do_finish(self):
        """ Set the flag for the status thread to finish execution
        """
        self.lock.acquire()
        self.run_status = False
        self.lock.release()

    def keep_running(self):
        """ Check if we continue running - the transfer is ongoing

        Returns:
            True if status thread should keep running, otherwise False
        """
        self.lock.acquire()
        ret = self.run_status
        self.lock.release()
        return ret


class Transfer(object):
    """ Trasfer archive object

    Attributes:
        req_json (JSON): Command received from the EOS MGM. Needs to contains the
            following entries: cmd, src, opt, uid, gid
        threads (list): List of threads doing parital transfers(CopyProcess jobs)
    """
    def __init__(self, req_json, config):
        self.config = config
        self.oper = req_json['cmd']
        self.uid, self.gid = req_json['uid'], req_json['gid']
        self.do_retry = (req_json['opt'] == self.config.OPT_RETRY)
        self.efile_full = req_json['src']
        self.efile_root = self.efile_full[:-(len(self.efile_full) - self.efile_full.rfind('/') - 1)]
        self.root_dir = self.efile_root[self.efile_root.rfind('//') + 1:]
        self.uuid = sha256(self.root_dir).hexdigest()
        local_file = join(self.config.DIR[self.oper], self.uuid)
        self.tx_file = local_file + ".tx"
        self.list_jobs, self.threads = [], []
        self.pid = os.getpid()
        self.archive = None
        # Special case for inital PUT as we need to copy also the archive file
        self.init_put = self.efile_full.endswith(self.config.ARCH_INIT)
        self.status = "initializing"
        self.lock_status = threading.Lock()
        self.timestamp = time.time()
        self.logger = logging.getLogger("transfer")
        self.thread_status = ThreadStatus(self)

    def get_status(self):
        """ Get current status

        Returns:
            String representing the status
        """
        self.lock_status.acquire()
        ret = self.status
        self.lock_status.release()
        return ret

    def set_status(self, msg):
        """ Set current status

        Args:
            msg (string): New status
        """
        self.lock_status.acquire()
        self.status = msg
        self.lock_status.release()

    def run(self):
        """ Run requested operation - fist call prepare

        Raises:
            IOError
        """
        self.thread_status.start()
        self.prepare()

        if self.oper in [self.config.PUT_OP, self.config.GET_OP]:
            self.do_transfer()
        elif self.oper in [self.config.PURGE_OP, self.config.DELETE_OP]:
            self.do_delete((self.oper == self.config.DELETE_OP))

    def prepare(self):
        """ Prepare requested operation.

        Raises:
            IOError: Failed to rename or transfer archive file.
        """
        # Rename archive file in EOS
        efile_url = client.URL(self.efile_full)
        eosf_rename = ''.join([self.efile_root, self.config.ARCH_FN, ".", self.oper, ".err"])
        rename_url = client.URL(eosf_rename)
        frename = [rename_url.protocol, "://", rename_url.hostid,
                   "//proc/user/?mgm.cmd=file&mgm.subcmd=rename"
                   "&mgm.path=", efile_url.path,
                   "&mgm.file.source=", efile_url.path,
                   "&mgm.file.target=", rename_url.path]
        (status, __, stderr) = exec_cmd(''.join(frename))

        if not status:
            err_msg = ("Failed to rename archive file {0} to {1}, msg={2}"
                       "").format(self.efile_full, rename_url, stderr)
            self.logger.error(err_msg)
            raise IOError(err_msg)

        # Copy archive file from EOS to the local disk
        self.efile_full = eosf_rename
        eos_fs = client.FileSystem(self.efile_full)
        st, _ = eos_fs.copy(self.efile_full + "?eos.ruid=0&eos.rgid=0",
                            self.tx_file, True)

        if not st.ok:
            err_msg = ("Failed to copy archive file={0} to local disk at={1}"
                       "").format(self.efile_full, self.tx_file)
            self.logger.error(err_msg)
            raise IOError(err_msg)

        # Create the ArchiveFile object
        d2t = (self.oper == self.config.PUT_OP)
        self.archive = ArchiveFile(self.tx_file, d2t)

    def do_delete(self, tape_delete):
        """ Delete archive either from disk (purge) or from tape (delete)

        Args:
            tape_delete (boolean): If true delete data from tape, otherwise
            from disk.

        Raises:
            IOError: Failed to delete an entry.
        """
        del_dirs = []
        self.logger.info("Do delete with tape_delete={0}".format(tape_delete))
        # Delete also the archive file saved on tape
        if tape_delete:
            self.archive.del_entry(self.config.ARCH_INIT, False, tape_delete)

        # First remove all the files and then the directories
        for fentry in self.archive.files():
            # d2t is false for both purge and deletion
            self.archive.del_entry(fentry[1], False, tape_delete)

        for dentry in self.archive.dirs():
            # Don't remove the root directory when purging
            if not tape_delete and dentry[1] == './':
                continue

            del_dirs.append(dentry[1])

        # Remove the directories from bottom up
        while len(del_dirs):
            dpath = del_dirs.pop()
            self.archive.del_entry(dpath, True, tape_delete)

        # Remove immutable flag from the EOS sub-tree
        if tape_delete:
            self.archive.make_mutable()

        self.clean_transfer(True)

    def do_transfer(self):
        """ Execute the put or get operation. What this method actually does is
        copy the JSON archive file from EOS to the local disk and read-in each
        entry, be it a file or a directory and creates it in the destination
        location. The archive file first contains the list of all the directories
        and then the files.

        Raises:
            IOError when an IO opperations fails.
        """
        t0 = time.time()
        indx_dir = 0
        err_entry = None

        # For retry get the first corrupted entry
        if self.do_retry:
            check_ok, err_entry = self.archive.verify()

            if check_ok:
                self.do_retry = False
                raise NoErrorException()

            # Delete the corrupted entry
            is_dir = (err_entry[0] == 'd')
            self.logger.info("Delete corrupted entry={0}".format(err_entry))

            if is_dir:
                self.archive.del_subtree(err_entry[1], None)
            else:
                self.archive.del_entry(err_entry[1], False, None)

        found_checkpoint = False  # flag set when reaching recovery entry

        # Create directories
        for dentry in self.archive.dirs():
            # Search for the recovery checkpoint
            if self.do_retry and not found_checkpoint:
                if dentry != err_entry:
                    indx_dir += 1
                    continue
                else:
                    found_checkpoint = True

            # Do special checks for root directory
            if not self.do_retry and dentry[1] == "./":
                self.archive.check_root_dir()

            indx_dir += 1
            self.archive.mkdir(dentry)
            msg = "create dir {0}/{1}".format(indx_dir, self.archive.header['num_dirs'])
            self.set_status(msg)

        # For GET issue the Prepare2Get for all the files on tape
        self.prepare2get(err_entry, found_checkpoint)

        # Copy files
        self.copy_files(err_entry, found_checkpoint)

        # For GET set file ownership and permissions
        self.update_file_access()
        self.set_status("verifying")
        check_ok, __ = self.archive.verify()
        self.set_status("cleaning")
        self.logger.info("TIMING_transfer={0} sec".format(time.time() - t0))
        self.clean_transfer(check_ok)

    def clean_transfer(self, check_ok):
        """ Clean the transfer by renaming the archive file in EOS adding the
        following extensions:
        .done - the transfer was successful
        .err  - there were errors during the transfer. These are logged in the
             file .archive.log in the same directory.

        Args:
            check_ok (bool): True if no error occured during transfer,
                otherwise false.
        """
        # Rename arch file in EOS to reflect the status
        if not check_ok:
            eosf_rename = ''.join([self.efile_root, self.config.ARCH_FN, ".", self.oper, ".err"])
        else:
            eosf_rename = ''.join([self.efile_root, self.config.ARCH_FN, ".", self.oper, ".done"])

        old_url = client.URL(self.efile_full)
        new_url = client.URL(eosf_rename)
        frename = [old_url.protocol, "://", old_url.hostid, "//proc/user/?",
                   "mgm.cmd=file&mgm.subcmd=rename&mgm.path=", old_url.path,
                   "&mgm.file.source=", old_url.path,
                   "&mgm.file.target=", new_url.path]
        (status, __, stderr) = exec_cmd(''.join(frename))

        if not status:
            err_msg = "Failed to rename {0} to {1}, msg={2}".format(
                self.efile_full, eosf_rename, stderr)
            self.logger.error(err_msg)
            # TODO: raise IOError
        else:
            # For successful delete operations remove also the archive file
            if self.oper == self.config.DELETE_OP and check_ok:
                fs = client.FileSystem(self.efile_full)
                st_rm, _ = fs.rm(new_url.path + "?eos.ruid=0&eos.rgid=0")

                if not st_rm.ok:
                    warn_msg = "Failed to delete archive {0}".format(new_url.path)
                    self.logger.warning(warn_msg)

        # Copy local log file back to EOS directory and set the ownership to the
        # identity of the client who triggered the archive
        dir_root = self.efile_root[self.efile_root.rfind('//') + 1:]
        eos_log = ''.join([old_url.protocol, "://", old_url.hostid, "/",
                           dir_root, self.config.ARCH_FN, ".log",
                           "?eos.ruid=", self.uid, "&eos.rgid=", self.gid])

        self.logger.debug("Copy log:{0} to {1}".format(self.config.LOG_FILE, eos_log))
        self.config.handler.flush()
        cp_client = client.FileSystem(self.efile_full)
        st, __ = cp_client.copy(self.config.LOG_FILE, eos_log, force=True)

        if not st.ok:
            self.logger.error("Failed to copy log file {0} to EOS at {1}".format(
                    self.config.LOG_FILE, eos_log))
        else:
            # delete log file if it was successfully copied to EOS
            try:
                os.remove(self.config.LOG_FILE)
            except OSError as __:
                pass

        # Delete all local files associated with this transfer
        try:
            os.remove(self.tx_file)
        except OSError as __:
            pass

        # Join async status thread
        self.thread_status.do_finish()
        self.thread_status.join()

    def copy_files(self, err_entry, found_checkpoint):
        """ Copy file from source to destination.

        Note that when doing put, the layout is not conserved. Therefore, a file
        with 3 replicas will end up as just a simple file in the new location.

        Args:
            err_entry (list): Entry record from the archive file corresponding
                 to the first file/dir that was corrupted.
            found_checkpoint (bool): If it's true, it means the checkpoint was
                 already found and we don't need to search for it.

        Raises:
            IOError: Copy request failed.
        """
        indx_file = 0
        # For inital PUT copy also the archive file to tape
        if self.init_put:
            dst = self.archive.header['dst'] + self.config.ARCH_INIT
            self.list_jobs.append((self.efile_full, dst))

        # Copy files
        for fentry in self.archive.files():
            # Search for the recovery checkpoint
            if self.do_retry and not found_checkpoint:
                if fentry != err_entry:
                    indx_file += 1
                    continue
                else:
                    found_checkpoint = True

            indx_file += 1
            msg = "copy file {0}/{1}".format(indx_file, self.archive.header['num_files'])
            self.set_status(msg)
            src, dst = self.archive.get_endpoints(fentry[1])

            # Copy file
            if not self.archive.d2t:
                # For GET we also have the dictionary with the metadata
                dfile = dict(zip(self.archive.header['file_meta'], fentry[2:]))
                dst = ''.join([dst, "?eos.ctime=", dfile['ctime'],
                               "&eos.mtime=", dfile['mtime'],
                               "&eos.bookingsize=", dfile['size'],
                               "&eos.targetsize=", dfile['size'],
                               "&eos.checksum=", dfile['xs'],
                               "&eos.ruid=0&eos.rgid=0"])
            else:
                # For PUT read the files from EOS as root
                src = ''.join([src, "?eos.ruid=0&eos.rgid=0"])

            self.logger.debug("Copying from {0} to {1}".format(src, dst))
            self.list_jobs.append((src, dst))

            if len(self.list_jobs) == self.config.BATCH_SIZE:
                st = self.flush_files(False)

                if not st:
                    err_msg = "Failed to flush files"
                    self.logger.error(err_msg)
                    raise IOError(err_msg)

        # Flush all pending copies and set metadata info for GET operation
        st = self.flush_files(True)

        if not st:
            err_msg = "Failed to flush files"
            self.logger.error(err_msg)
            raise IOError(err_msg)

    def flush_files(self, wait_all):
        """ Flush all pending transfers from the list of jobs.

        Args:
            wait_all (bool): If true wait and collect the status from all
                executing threads.

        Returns:
            True if files flushed successfully, otherwise false.
        """
        status = True

        # Wait until a thread from the pool gets freed if we reached the maximum
        # allowed number of running threads
        while len(self.threads) >= self.config.MAX_THREADS:
            for indx, thread in enumerate(self.threads):
                thread.join(self.config.JOIN_TIMEOUT)

                # If thread finished get the status and mark it for removal
                if not thread.isAlive():
                    status = status and thread.xrd_status.ok
                    self.logger.debug("Thread={0} status={1}".format(
                            thread.ident, thread.xrd_status.ok))

                    if not thread.xrd_status.ok:
                        self.logger.error("Thread={0} err_msg={1}".format(
                                thread.ident, thread.xrd_status.message))

                    del self.threads[indx]
                    break

        # If previous transfers were successful and we still have jobs
        if status and self.list_jobs:
            proc = client.CopyProcess()

            for job in self.list_jobs:
                # TODO: use the parallel mode starting with XRootD 4.1
                proc.add_job(job[0], job[1], force=self.do_retry, thirdparty=True)

            del self.list_jobs[:]
            thread = ThreadJob(proc)
            thread.start()
            self.threads.append(thread)

        # If we already have failed transfers or we submitted all the jobs then
        # join the rest of the threads and collect also their status
        if not status or wait_all:
            for thread in self.threads:
                thread.join()
                status = status and thread.xrd_status.ok
                self.logger.debug("Thread={0} status={1}".format(
                        thread.ident, thread.xrd_status.ok))

                if not thread.xrd_status.ok:
                    self.logger.debug("Thread={0} status={1}".format(
                            thread.ident, thread.xrd_status.message))

            del self.threads[:]

        return status

    def update_file_access(self):
        """ Set the ownership and the permissions for the files copied to EOS.
        This is done only for GET operation i.e. self.archive.d2t == False.

        Raises:
            IOError: chown or chmod operations failed
        """
        if self.archive.d2t:
            return

        self.set_status("updating file access")
        t0 = time.time()
        oper = 'query'
        metahandler = MetaHandler()
        fs = self.archive.fs_src

        for fentry in self.archive.files():
            __, surl = self.archive.get_endpoints(fentry[1])
            url = client.URL(surl)
            dict_meta = dict(zip(self.archive.header['file_meta'], fentry[2:]))

            # Send the chown async request
            arg = ''.join([url.path, "?eos.ruid=0&eos.rgid=0&mgm.pcmd=chown&uid=",
                           dict_meta['uid'], "&gid=", dict_meta['gid']])
            xrd_st = fs.query(QueryCode.OPAQUEFILE, arg,
                              callback=metahandler.register(oper, surl))

            if not xrd_st.ok:
                __ = metahandler.wait(oper)
                err_msg = "Failed query chown for path={0}".format(surl)
                self.logger.error(err_msg)
                raise IOError(err_msg)

            # Send the chmod async request
            mode = int(dict_meta['mode'], 8) # mode is saved in octal format
            arg = ''.join([url.path, "?eos.ruid=0&eos.rgid=0&mgm.pcmd=chmod&mode=",
                           str(mode)])
            xrd_st = fs.query(QueryCode.OPAQUEFILE, arg,
                              callback=metahandler.register(oper, surl))

            if not xrd_st.ok:
                __ = metahandler.wait(oper)
                err_msg = "Failed query chmod for path={0}".format(surl)
                self.logger.error(err_msg)
                raise IOError(err_msg)

        status  = metahandler.wait(oper)

        if status:
            t1 = time.time()
            self.logger.info("TIMING_update_file_access={0} sec".format(t1 - t0))
        else:
            err_msg = "Failed update file access"
            self.logger.error(err_msg)
            raise IOError(err_msg)

    def prepare2get(self, err_entry, found_checkpoint):
        """This method is only executed for GET operations and it's purpose is
        to issue the Prepapre2Get commands for the files in the archive which
        will later on be copied back to EOS.

        Args:
            err_entry (list): Entry record from the archive file corresponding
                 to the first file/dir that was corrupted.
            found_checkpoint (bool): If it's true, it means the checkpoint was
                 already found and we don't need to search for it.

        Raises:
            IOError: The Prepare2Get request failed.
        """
        if self.archive.d2t:
            return

        limit = 20  # max files per prepare request
        oper = 'prepare'
        self.set_status("prepare2get")
        t0 = time.time()
        lpaths = []
        metahandler = MetaHandler()

        for fentry in self.archive.files():
            # Find error checkpoint if not already found
            if err_entry and not found_checkpoint:
                if fentry != err_entry:
                    continue
                else:
                    found_checkpoint = True

            surl, __ = self.archive.get_endpoints(fentry[1])
            lpaths.append(surl[surl.rfind('//') + 1:])

            if len(lpaths) == limit:
                xrd_st = self.archive.fs_dst.prepare(lpaths, PrepareFlags.STAGE,
                            callback=metahandler.register(oper, surl))

                if not xrd_st.ok:
                    __ = metahandler.wait(oper)
                    err_msg = "Failed prepare2get for path={0}".format(surl)
                    self.logger.error(err_msg)
                    raise IOError(err_msg)

                del lpaths[:]

        # Send the remaining requests
        if lpaths:
            xrd_st = self.archive.fs_dst.prepare(lpaths, PrepareFlags.STAGE,
                        callback=metahandler.register(oper, surl))

            if not xrd_st.ok:
                __ = metahandler.wait(oper)
                err_msg = "Failed prepare2get"
                self.logger.error(err_msg)
                raise IOError(err_msg)

            del lpaths[:]

        status  = metahandler.wait(oper)

        if status:
            t1 = time.time()
            self.logger.info("TIMING_prepare2get={0} sec".format(t1 - t0))
        else:
            err_msg = "Failed prepare2get"
            self.logger.error(err_msg)
            raise IOError(err_msg)