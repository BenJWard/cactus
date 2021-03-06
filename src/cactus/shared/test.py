#!/usr/bin/env python

#Copyright (C) 2009-2011 by Benedict Paten (benedictpaten@gmail.com)
#
#Released under the MIT license, see LICENSE.txt
"""Common test functions used for generating inputs to run cactus workflow and running
cactus workflow and the various utilities.
"""

import random
import os
import xml.etree.ElementTree as ET

from sonLib.bioio import logger
from sonLib.bioio import getTempFile
from sonLib.bioio import getTempDirectory
from sonLib.bioio import getRandomSequence
from sonLib.bioio import mutateSequence
from sonLib.bioio import reverseComplement
from sonLib.bioio import fastaRead, fastaWrite
from sonLib.bioio import printBinaryTree
from sonLib.bioio import system
from sonLib.bioio import getRandomAlphaNumericString
from sonLib.bioio import cigarWrite, PairwiseAlignment, AlignmentOperation

from cactus.shared.common import runCactusWorkflow
from cactus.shared.common import runCactusCheck

from sonLib.bioio import TestStatus

from sonLib.tree import makeRandomBinaryTree

from cactus.shared.common import runToilStats, runToilStatusAndFailIfNotComplete

from cactus.shared.experimentWrapper import DbElemWrapper
from cactus.shared.experimentWrapper import ExperimentWrapper

def silentOnSuccess(fn):
    """
    Redirect stdout, stderr for a test function, then output them if it fails.
    """
    def wrap(self):
        # Pretty much ripped from the toil worker.py setup.
        tempPath = getTempFile()
        oldStdout = os.dup(1)
        oldStderr = os.dup(2)

        #Open the file to send stdout/stderr to.
        logFh = os.open(tempPath, os.O_RDWR | os.O_CREAT | os.O_APPEND)

        #Replace standard output with a descriptor for the log file
        os.dup2(logFh, 1)

        #Replace standard error with a descriptor for the log file
        os.dup2(logFh, 2)
        try:
            fn(self)
        except:
            oldStdoutFile = os.fdopen(oldStdout, 'w')
            logFile = os.fdopen(os.dup(logFh))
            logFile.seek(0)
            oldStdoutFile.write(logFile.read())
            raise
        finally:
            # Close the descriptor we used to open the file
            os.close(logFh)
            # Reset stdout and stderr
            os.dup2(oldStdout, 1)
            os.dup2(oldStderr, 2)
            os.remove(tempPath)
    return wrap

###########
#Stuff for setting up the experiment configuration file for a test
###########

_GLOBAL_DATABASE_CONF_STRING = '<st_kv_database_conf type="kyoto_tycoon"><kyoto_tycoon in_memory="1" port="1978" snapshot="0"/></st_kv_database_conf>'
_BATCH_SYSTEM = None

def getBatchSystem():
    return _BATCH_SYSTEM

def getGlobalDatabaseConf():
    return _GLOBAL_DATABASE_CONF_STRING

def initialiseGlobalDatabaseConf(dataString):
    """Initialises the global database conf string which, if defined,
    is used as the database for CactusWorkflowExperiments."""
    global _GLOBAL_DATABASE_CONF_STRING
    _GLOBAL_DATABASE_CONF_STRING = dataString

def initialiseGlobalBatchSystem(batchSystem):
    """Initialise the global batch system variable.
    """
    global _BATCH_SYSTEM
    assert batchSystem in ("singleMachine", "parasol", "gridEngine")
    _BATCH_SYSTEM = batchSystem
    
def getCactusWorkflowExperimentForTest(sequences, newickTreeString, outputDir, configFile=None,
                                       constraints=None, progressive=False):
    """Wrapper to constructor of CactusWorkflowExperiment which additionally incorporates
    any globally set database conf.
    """
    halFile = os.path.join(outputDir, "test.hal")
    fastaFile = os.path.join(outputDir, "test.fa")
    databaseConf = ET.fromstring(_GLOBAL_DATABASE_CONF_STRING) if _GLOBAL_DATABASE_CONF_STRING is not None else None
    return ExperimentWrapper.createExperimentWrapper(sequences, newickTreeString, outputDir,
                                    databaseConf=databaseConf, configFile=configFile,
                                    halFile=halFile, fastaFile=fastaFile, constraints=constraints, progressive=progressive)

###############
#Stuff for getting random inputs to a test
###############

def getCactusInputs_random(regionNumber=0, tempDir=None,
                           sequenceNumber=None,
                           avgSequenceLength=None,
                           treeLeafNumber=None):
    """Gets a random set of sequences, each of length given, and a species
    tree relating them. Each sequence is a assigned an event in this tree.
    """
    if sequenceNumber is None:
        sequenceNumber = random.choice(xrange(30))
    if avgSequenceLength is None:
        avgSequenceLength = random.choice(xrange(1,3000))
    if treeLeafNumber is None:
        treeLeafNumber = random.choice(xrange(2, 4))
    #Make tree
    binaryTree = makeRandomBinaryTree(treeLeafNumber)
    newickTreeString = printBinaryTree(binaryTree, includeDistances=True)
    newickTreeLeafNames = []
    def fn(tree):
        if tree.internal:
            fn(tree.left)
            fn(tree.right)
        else:
            newickTreeLeafNames.append(tree.iD)
    fn(binaryTree)
    logger.info("Made random binary tree: %s" % newickTreeString)
    
    sequenceDirs = []
    for i in xrange(len(newickTreeLeafNames)):
        seqDir = getTempDirectory(rootDir=tempDir)
        sequenceDirs.append(seqDir)

    logger.info("Made a set of random directories: %s" % " ".join(sequenceDirs))

    #Random sequences and species labelling
    sequenceFile = None
    fileHandle = None
    parentSequence = getRandomSequence(length=random.choice(xrange(1, 2*avgSequenceLength)))[1]
    emptySequenceDirs = set(sequenceDirs)
    i = 0
    while i < sequenceNumber or len(emptySequenceDirs) > 0:
        #for i in xrange(sequenceNumber):
        if sequenceFile == None:
            if random.random() > 0.5: #Randomly choose the files to be attached or not
                suffix = ".fa.complete"
            else:
                suffix = ".fa"
            sequenceDir = random.choice(sequenceDirs)
            if sequenceDir in emptySequenceDirs:
                emptySequenceDirs.remove(sequenceDir)
            sequenceFile = getTempFile(rootDir=sequenceDir, suffix=suffix)
            fileHandle = open(sequenceFile, 'w')
        if random.random() > 0.8: #Get a new root sequence
            parentSequence = getRandomSequence(length=random.choice(xrange(1, 2*avgSequenceLength)))[1]
        sequence = mutateSequence(parentSequence, distance=random.random()*0.5)
        name = getRandomAlphaNumericString(15)
        if random.random() > 0.5:
            sequence = reverseComplement(sequence)
        fastaWrite(fileHandle, name, sequence)
        if random.random() > 0.5:
            fileHandle.close()
            fileHandle = None
            sequenceFile = None
        i += 1
    if fileHandle != None:
        fileHandle.close()

    logger.info("Made %s sequences in %s directories" % (sequenceNumber, len(sequenceDirs)))
    
    return sequenceDirs, newickTreeString

def getFastasFromSequence(sequenceDirs):
    #Get the sequences
    fastaSeqs = []
    for sequenceDir in sequenceDirs:
        for fastaFile in os.listdir(sequenceDir):
            fileHandle = open(os.path.join(sequenceDir, fastaFile), 'r')
            for name, sequence in fastaRead(fileHandle):
                fastaSeqs.append((name, sequence))
            fileHandle.close()
    return fastaSeqs

def makeRandomConstraints(fastaSeqs):
    #Now make the fake alignments and write to file
    constraints = []
    if len(fastaSeqs) > 0:
        for i in xrange(random.randint(0, 1000)):
            name1, sequence1 = random.choice(fastaSeqs)
            name2, sequence2 = random.choice(fastaSeqs)
            if len(sequence1) == 0 or len(sequence2) == 0:
                continue
            def getRandomInterval(sequence):
                start = random.randint(0, len(sequence)-1)
                if random.random() > 0.5:
                    return start, start+1, True
                return start+1, start, False
            start1, end1, strand1 = getRandomInterval(sequence1)
            start2, end2, strand2 = getRandomInterval(sequence2)
            constraints.append(PairwiseAlignment(name1, start1, end1, strand1, 
                                                  name2, start2, end2, strand2, 
                                                  1000, [ AlignmentOperation(PairwiseAlignment.PAIRWISE_MATCH, 1, 1000)]))
    return constraints

def getCactusInputs_randomWithConstraints(regionNumber=0, tempDir=None):
    sequenceDirs, newickTreeString = getCactusInputs_random(regionNumber=regionNumber, tempDir=tempDir)
    constraints = getTempFile(rootDir=tempDir)
    fileHandle = open(constraints, 'w')
    for pairwiseAlignment in makeRandomConstraints(getFastasFromSequence(sequenceDirs)):
        cigarWrite(fileHandle, pairwiseAlignment, withProbs=False)
    fileHandle.close()
    return sequenceDirs, newickTreeString, constraints

def parseNewickTreeFile(newickTreeFile):
    fileHandle = open(newickTreeFile, 'r')
    newickTreeString = fileHandle.readline()
    fileHandle.close()
    return newickTreeString

def getInputs(path, sequenceNames):
    """Requires setting SON_TRACE_DATASETS variable and having access to datasets.
    """
    seqPath = os.path.join(TestStatus.getPathToDataSets(), path)
    sequences = [ os.path.join(seqPath, sequence) for sequence in sequenceNames ] #Same order as tree
    newickTreeString = parseNewickTreeFile(os.path.join(path, "tree.newick"))
    return sequences, newickTreeString  

def getCactusInputs_blanchette(regionNumber=0, tempDir=None):
    """Gets the inputs for running cactus_workflow using a blanchette simulated region
    (0 <= regionNumber < 50).
    
    Requires setting SON_TRACE_DATASETS variable and having access to datasets.
    """
    assert regionNumber >= 0
    assert regionNumber < 50
    blanchettePath = os.path.join(TestStatus.getPathToDataSets(), "blanchettesSimulation")
    sequences = [os.path.join(blanchettePath, ("%.2i.job" % regionNumber), species) \
                 for species in ("HUMAN", "CHIMP", "BABOON", "MOUSE", "RAT", "DOG", "CAT", "PIG", "COW")] #Same order as tree
    newickTreeString = parseNewickTreeFile(os.path.join(blanchettePath, "tree.newick"))
    return sequences, newickTreeString

def getCactusInputs_funkyHeaderNames(regionNumber=0, tempDir=None):
    """Gets inputs (based on Blanchette region 0) that have weird header names
    that might get parsed wrong and cause issues."""
    sequences, newickTreeString = getCactusInputs_blanchette(regionNumber=regionNumber)

    # Assign weird header names
    if tempDir is None:
        tempDir = getTempDir()
    # Should also consider "bar foo", "ba rfoo", but we currently
    # throw away everything but the first token (probably because of
    # cigar parsing).
    funkyHeaderNames = ['id=1|foo', 'test1|1600', 'test2|', '|test3', 'id=1|bar']
    funkyIndex = 0
    for i, sequencePath in enumerate(sequences):
        newPath = os.path.join(tempDir, str(i))
        for _, sequence in fastaRead(sequencePath):
            header = funkyHeaderNames[funkyIndex % len(funkyHeaderNames)]
            funkyIndex += 1
            fastaWrite(newPath, header, sequence, 'a')
        sequences[i] = newPath

    return sequences, newickTreeString

def getCactusInputs_encode(regionNumber=0, tempDir=None):
    """Gets the inputs for running cactus_workflow using an Encode pilot project region.
     (0 <= regionNumber < 15).
    
    Requires setting SON_TRACE_DATASETS variable and having access to datasets.
    """
    assert regionNumber >= 0
    assert regionNumber < 14
    encodeRegionString = "ENm%03i" % (regionNumber+1)
    encodeDatasetPath = os.path.join(TestStatus.getPathToDataSets(), "MAY-2005")
    sequences = [ os.path.join(encodeDatasetPath, encodeRegionString, ("%s.%s.fa" % (species, encodeRegionString))) for\
                species in ("human", "chimp", "baboon", "mouse", "rat", "dog", "cow") ]
    newickTreeString = parseNewickTreeFile(os.path.join(encodeDatasetPath, "reducedTree.newick"))
    return sequences, newickTreeString

def getCactusInputs_chromosomeX(regionNumber=0, tempDir=None):
    """Gets the inputs for running cactus_workflow using an some mammlian chromosome
    X's.
    
    Requires setting SON_TRACE_DATASETS variable and having access to datasets.
    """
    chrXPath = os.path.join(TestStatus.getPathToDataSets(), "chr_x")
    sequences = [ os.path.join(chrXPath, seqFile) for seqFile in ("cow.fa", "dog.fa", "human.fa", "mouse.fa", "rat.fa") ]
    newickTreeString = parseNewickTreeFile(os.path.join(chrXPath, "newickTree.txt"))
    return sequences, newickTreeString

def getCactusInputs_evolverMammals():
    """Gets the inputs for running cactus_workflow using some simulated, half megabase mammlian chromosomes.
    
    Requires setting SON_TRACE_DATASETS variable and having access to datasets.
    """
    evolverPath = os.path.join(TestStatus.getPathToDataSets(), "evolver", "mammals", "loci1")
    sequences = [ os.path.join(evolverPath, seqFile) for seqFile in ("simHuman.chr6", "simMouse.chr6", "simRat.chr6", "simCow.chr6", "simDog.chr6") ]
    newickTreeString = parseNewickTreeFile(os.path.join(evolverPath, "tree.newick"))
    return sequences, newickTreeString

def getCactusInputs_evolverPrimates():
    """Gets the inputs for running cactus_workflow using some simulated, half megabase primate chromosomes.
    
    Requires setting SON_TRACE_DATASETS variable and having access to datasets.
    """
    evolverPath = os.path.join(TestStatus.getPathToDataSets(), "evolver", "primates", "loci1")
    sequences = [ os.path.join(evolverPath, seqFile) for seqFile in ("simHuman.chr6", "simChimp.chr6", "simGorilla.chr6" , "simOrang.chr6") ]
    newickTreeString = parseNewickTreeFile(os.path.join(evolverPath, "tree.newick"))
    return sequences, newickTreeString

def runWorkflow_TestScript(sequences, newickTreeString, 
                           outputDir=None,
                           batchSystem="single_machine",
                           buildAvgs=False, 
                           buildReference=False,
                           buildHal=False,
                           buildFasta=False,
                           configFile=None,
                           buildToilStats=False,
                           constraints=None,
                           progressive=False,
                           cactusWorkflowFunction=runCactusWorkflow):
    """Runs the workflow and various downstream utilities.
    """
    logger.info("Running cactus workflow test script")
    logger.info("Got the following sequence dirs/files: %s" % " ".join(sequences))
    logger.info("Got the following tree %s" % newickTreeString)
    
    #Setup the output dir
    assert outputDir != None
    logger.info("Using the output dir: %s" % outputDir)
    
    #Setup the flower disk.
    experiment = getCactusWorkflowExperimentForTest(sequences, newickTreeString, 
                                                    outputDir=outputDir,
                                                    configFile=configFile, constraints=constraints,
                                                    progressive=progressive)
    experimentFile = os.path.join(outputDir, "experiment.xml")
    experiment.writeXML(experimentFile)
    logger.info("The experiment file %s\n" % experimentFile)
   
    #Setup the job tree dir.
    toilDir = os.path.join(outputDir, "toil")
    logger.info("Got a job tree dir for the test: %s" % toilDir)
    
    #Run the actual workflow
    cactusWorkflowFunction(experimentFile, toilDir, 
                           batchSystem=batchSystem, buildAvgs=buildAvgs,
                           buildReference=buildReference,
                           buildHal=buildHal,
                           buildFasta=buildFasta,
                           toilStats=buildToilStats,
                           logLevel="DEBUG")
    logger.info("Ran the the workflow")
    
    #Now run various utilities..
    if buildToilStats:
        toilStatsFile = os.path.join(outputDir, "toilStats.xml")
        runToilStats(toilDir, toilStatsFile)
        
    #Now remove everything we generate
    system("rm -rf %s %s" % (toilDir, experimentFile))   
    
    #Return so calling function can cleanup
    return experiment
        
testRestrictions_NotShort = ()
        
def runWorkflow_multipleExamples(inputGenFunction,
                                 testNumber=1, 
                                 testRestrictions=(TestStatus.TEST_SHORT, TestStatus.TEST_MEDIUM, \
                                                   TestStatus.TEST_LONG, TestStatus.TEST_VERY_LONG,),
                                 inverseTestRestrictions=False,
                                 batchSystem="single_machine",
                                 buildAvgs=False, buildReference=False,
                                 configFile=None, buildToilStats=False, 
                                 useConstraints=False,
                                 cactusWorkflowFunction=runCactusWorkflow,
                                 buildHal=False,
                                 buildFasta=False,
                                 progressive=False):
    """A wrapper to run a number of examples.
    """
    if (inverseTestRestrictions and TestStatus.getTestStatus() not in testRestrictions) or \
        (not inverseTestRestrictions and TestStatus.getTestStatus() in testRestrictions):
        for test in xrange(testNumber): 
            tempDir = getTempDirectory(os.getcwd())
            if useConstraints:
                sequences, newickTreeString, constraints = inputGenFunction(regionNumber=test, tempDir=tempDir)
            else:
                sequences, newickTreeString = inputGenFunction(regionNumber=test, tempDir=tempDir)
                constraints = None
            runWorkflow_TestScript(sequences, newickTreeString,
                                   outputDir=tempDir,
                                   batchSystem=batchSystem,
                                   buildAvgs=buildAvgs, buildReference=buildReference,
                                   buildHal=buildHal,
                                   buildFasta=buildFasta,
                                   configFile=configFile,
                                   buildToilStats=buildToilStats,
                                   constraints=constraints,
                                   progressive=progressive,
                                   cactusWorkflowFunction=cactusWorkflowFunction)
            system("rm -rf %s" % tempDir)
            logger.info("Finished random test %i" % test)

def checkCigar(filename):
    lines = 0
    with open(filename, 'r') as fh:
        for line in fh:
            lines += 1
            if not (line.startswith("cigar:") or line.startswith("#") or line == ""):
                raise RuntimeError("Illegal line found in cigar file.")
    if lines == 0:
        raise RuntimeError("Cigar file is empty.")
