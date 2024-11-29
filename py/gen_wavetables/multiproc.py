import os,subprocess,logging,multiprocessing,queue,yaml,types,time


class processor(multiprocessing.Process):
    def __init__(self,procId,iQ,oQ,procArgs,procFunc):
        super(processor,self).__init__()
        self.procId    = procId     # the id of this process
        self.iQ        = iQ         # process input queue (shared by all processes)
        self.oQ        = oQ         # process output queue (shared by all processes)
        self.cycleIdx  = 0          # count of times the user function has been called
        self.procArgs  = procArgs   # arguments to the user defined function (procFunc) that are used for the life of the process
        self.procFunc  = procFunc   # user defined function this process will execute
        
    def run(self):
        super(processor,self).run()

        self.cycleIdx = 0

        # loop until the user process returns false
        while True:

            # if no msg is available
            if self.iQ.empty():                
                time.sleep(0.1)

            # attempt to get the message
            try:
                msg = self.iQ.get(block=False)
            except queue.Empty: 
                continue # the dequeue attempt failed

            # if the message is 'None' then end the process
            if msg == None:
                break
            
            # run the user function
            r = self._func(msg)
                        
            # send the result of the function back to the main process
            self.oQ.put(r)
            
            self.cycleIdx += 1

    def _func(self,taskArgs):
        resultD = self.procFunc( self.procId, self.procArgs, taskArgs )

        return resultD



def _local_distribute_dispatcher( inQ, outQ, processN, taskArgsL, procArgs, processArgsFunc, processResultFunc, verboseLevel ):
        
    bestScore  = None
    iterN      = 0  # total count of jobs completed and pending
    pendingN   = 0  # total count of jobs pending
    nextSrcIdx = 0
    resultL    = []
    t0         = time.time()

    while len(resultL) < len(taskArgsL):

        # if available processes exist and all source files have not been sent for processing already
        if pendingN < processN and nextSrcIdx < len(taskArgsL):

            # if a args processing function was given 
            args = taskArgsL[nextSrcIdx]
            if processArgsFunc is not None:
                args = processArgsFunc(procArgs,args)
                
            inQ.put( args )
            
            nextSrcIdx += 1
            pendingN   += 1
            t0         = time.time()

            if verboseLevel>=3:
                print(f"Send: remaining:{len(taskArgsL)-nextSrcIdx} pend:{pendingN} result:{len(resultL)}")

            
        # if a process completed
        elif not outQ.empty():
            
            # attempt to get the message
            try:
                resultD = outQ.get(block=False)
            except queue.Empty:
                if verboseLevel > 0:
                    print("*********  A message dequeue attempt failed.")
                continue # the dequeue attempt failed

            # if a result processing function was given
            if processResultFunc is not None:
                resultD = processResultFunc( procArgs, resultD )
                
            resultL.append(resultD)

            pendingN -= 1
            t0        = time.time()

            if verboseLevel>=3:
                print(f"Recv: remaining:{len(taskArgsL)-nextSrcIdx} pend:{pendingN} result:{len(resultL)}")

            
        # nothing to do - sleep
        else:
            time.sleep(0.1)


            t1 = time.time()
            if t1 - t0 > 60:
                if verboseLevel >= 2:
                    print(f"Wait: remaining:{len(taskArgsL)-nextSrcIdx} pend:{pendingN} result:{len(resultL)}")
                t0 = t1

    return resultL

    
def local_distribute_main(processN, procFunc, procArgs, taskArgsL, processArgsFunc=None, processResultFunc=None, verboseLevel=3):
    """ Distribute the function 'procFunc' to 'procN' local processes.
    This function will call procFunc(procArgs,taskArgsL[i]) len(taskArgsL) times
    and return the result of each call in the list resultL[].
    The function will be run in processN parallel processes.
    Input:

    :processN: Count of processes to run in parallel.

    :procFunc: A python function of the form: myProc(procId,procArgs,taskArgsL[i]).
    This function is run in a remote process.

    :procArgs: A data structure holding read-only arguments which are fixed accross all processes.
    This data structure is duplicated on all remote processes.

    :taskArgsL: A list of data structures holding the per-call arguments to 'procFunc()'.
    Note that taskArgsL[i] may never be 'None' because None is used by the 
    processes control system to indicate that the process should be shutdown.

    :processArgsFunc: A function of the form args = processArgsFunc(procArgs,args)
    which can be used to modify the arg. record from taskArgssL[] prior to the call
    to 'procFunc()'.  This function runs locally in the calling functions process.

    :processResultFunc: A function of the form result = processResulftFunc(procArgs,result).
    which is called on the result of procFunc() prior to the result being store in the
    return result list. This function runs locally in the calling functions process.
    """

    processN = processN
    mgr      = multiprocessing.Manager()
    inQ      = mgr.Queue()
    outQ     = mgr.Queue()
    processL = []

    # create and start the processes
    for i in range(processN):
        pr  = processor(i,inQ,outQ,procArgs,procFunc)
        processL.append( pr )        
        pr.start()

    # service the processes
    resultL = _local_distribute_dispatcher(inQ, outQ, processN, taskArgsL, procArgs, processArgsFunc, processResultFunc, verboseLevel)

    # tell the processes to stop
    for pr in processL:
        inQ.put(None)

    # join the processes
    for pr in processL:        
        while True:
            pr.join(1.0)
            if pr.is_alive():
                time.sleep(1)
            else:
                break
                
    return resultL



        




