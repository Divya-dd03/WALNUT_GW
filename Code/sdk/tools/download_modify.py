import os, sys

mark = {"fs":"nvm","rf":"rfbin","app":"customer_app"}

class download_modify():
    def __init__(self, srcFile, dstFile, mode, commandpartitions):
        self.srcFile = srcFile
        self.dstFile = dstFile
        self.mode = mode
        self.commandpartitions = commandpartitions[:]
        self.modify()

    def modify(self):
        commandpartitions = []
        for cp in self.commandpartitions:
            i = cp.find(":")
            command = cp[:i]
            partition = cp[i+1:]
            try:
                partition = mark[str(partition)]
            except Exception:
                pass

            commandpartition = {}
            commandpartition["commond"] = command
            commandpartition["partition"] = partition
            commandpartitions.append(commandpartition)
        # print(commandpartitions)

        fileSrc = open(self.srcFile,"r")
        dataSrc = fileSrc.read()
        fileSrc.close()
        cpSrc = ""
        cpSrcs = []
        i = 0
        while i < len(dataSrc):
            cpSrc += dataSrc[i]
            if dataSrc[i] == "}":
                cpSrcs.append(cpSrc)
                cpSrc = ""
            i+=1

        for i in range(0, len(cpSrcs)):
            if "\"command\" : \"flash\"" in cpSrcs[i] :
                cpDsts = cpSrcs[0:i]
                cpDstDeals = cpSrcs[i:]
                break

        for i in range(0, len(commandpartitions)):
            commandStr = "\"command\" : \"" + str(commandpartitions[i]["commond"]) + "\""
            partitionStr = "\"partition\" : \"" + str(commandpartitions[i]["partition"]) + "\""
            for j in range(0, len(cpDstDeals)):
                if (commandStr in cpDstDeals[j]) and (partitionStr in cpDstDeals[j]):
                    if self.mode=="keep":
                        cpDsts.append(cpDstDeals[j])
                    else:
                        cpDstDeals[j] = ""
        if not self.mode=="keep":
            cpDsts += cpDstDeals

        fileDst = open(self.dstFile,"w")
        for cpDst in cpDsts:
            fileDst.write(str(cpDst))
        fileDst.write("\n]")
        fileDst.close()

if __name__ == '__main__':
    srcFile = sys.argv[1]
    dstFile = sys.argv[2]
    mode = sys.argv[3]
    
    commandpartitions = []
    for i in sys.argv[4:]:
        commandpartitions.append(i)

    # print(srcFile)
    # print(dstFile)
    # print(mode)
    # print(commandpartitions)
    download_modify(srcFile, dstFile, mode, commandpartitions)