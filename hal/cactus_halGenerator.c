/*
 * Copyright (C) 2009-2011 by Benedict Paten (benedictpaten@gmail.com)
 *
 * Released under the MIT license, see LICENSE.txt
 */

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>

#include "cactus.h"
#include "sonLib.h"
#include "hal.h"

void usage() {
    fprintf(stderr, "cactus_halGenerator [flower names], version 0.1\n");
    fprintf(stderr, "-a --logLevel : Set the log level\n");
    fprintf(stderr,
            "-c --cactusDisk : The location of the flower disk directory\n");
    fprintf(stderr, "-c --secondaryDisk : The location of secondary disk\n");
    fprintf(stderr,
            "-g --referenceEventString : String identifying the reference event.\n");
    fprintf(stderr, "-k --outputFile : File to put final output in.\n");
    fprintf(
            stderr,
            "-l --showOnlySubstitutionsWithRespectToReference : Put stars in place of characters that are identical to the reference.\n");
    fprintf(stderr, "-h --help : Print this help screen\n");
}

int main(int argc, char *argv[]) {
    /*
     * Script for adding a reference genome to a flower.
     */

    /*
     * Arguments/options
     */
    char * logLevelString = NULL;
    char * cactusDiskDatabaseString = NULL;
    char * secondaryDatabaseString = NULL;
    char *referenceEventString =
            (char *) cactusMisc_getDefaultReferenceEventHeader();
    char *outputFile = NULL;

    ///////////////////////////////////////////////////////////////////////////
    // (0) Parse the inputs handed by genomeCactus.py / setup stuff.
    ///////////////////////////////////////////////////////////////////////////

    while (1) {
        static struct option long_options[] = { { "logLevel",
                required_argument, 0, 'a' }, { "cactusDisk", required_argument,
                0, 'c' }, { "secondaryDisk", required_argument, 0, 'e' },
                { "referenceEventString", required_argument, 0, 'g' }, {
                        "help", no_argument, 0, 'h' }, { "outputFile",
                        required_argument, 0, 'k' }, {
                        "showOnlySubstitutionsWithRespectToReference",
                        no_argument, 0, 'l' },
                { 0, 0, 0, 0 } };

        int option_index = 0;

        int key = getopt_long(argc, argv, "a:c:d:e:g:hk:l", long_options,
                &option_index);

        if (key == -1) {
            break;
        }

        switch (key) {
            case 'a':
                logLevelString = stString_copy(optarg);
                break;
            case 'c':
                cactusDiskDatabaseString = stString_copy(optarg);
                break;
            case 'e':
                secondaryDatabaseString = stString_copy(optarg);
                break;
            case 'g':
                referenceEventString = stString_copy(optarg);
                break;
            case 'h':
                usage();
                return 0;
            case 'k':
                outputFile = stString_copy(optarg);
                break;
            default:
                usage();
                return 1;
        }
    }

    ///////////////////////////////////////////////////////////////////////////
    // (0) Check the inputs.
    ///////////////////////////////////////////////////////////////////////////

    assert(cactusDiskDatabaseString != NULL);

    //////////////////////////////////////////////
    //Set up logging
    //////////////////////////////////////////////

    st_setLogLevelFromString(logLevelString);

    //////////////////////////////////////////////
    //Load the database
    //////////////////////////////////////////////

    stKVDatabaseConf *kvDatabaseConf = stKVDatabaseConf_constructFromString(
            cactusDiskDatabaseString);
    CactusDisk *cactusDisk = cactusDisk_construct(kvDatabaseConf, false, true);
    stKVDatabaseConf_destruct(kvDatabaseConf);
    st_logInfo("Set up the flower disk\n");

    //////////////////////////////////////////////
    //Load the secondary database
    //////////////////////////////////////////////

    kvDatabaseConf = stKVDatabaseConf_constructFromString(
                secondaryDatabaseString);
    stKVDatabase *sequenceDatabase = stKVDatabase_construct(kvDatabaseConf, 0);
    stKVDatabaseConf_destruct(kvDatabaseConf);
    st_logInfo("Set up the secondary database\n");

    FlowerStream *flowerStream = flowerWriter_getFlowerStream(cactusDisk, stdin);
    if (outputFile != NULL && flowerStream_size(flowerStream) != 1) {
        stThrowNew("RUNTIME_ERROR",
                   "Output file specified, but there is more than one flower\n");
    }
    Flower *flower;
    while ((flower = flowerStream_getNext(flowerStream)) != NULL) {
        ///////////////////////////////////////////////////////////////////////////
        // Get the reference event name
        ///////////////////////////////////////////////////////////////////////////

        Event *referenceEvent = eventTree_getEventByHeader(
            flower_getEventTree(flower), referenceEventString);
        assert(referenceEvent != NULL);
        Name referenceEventName = event_getName(referenceEvent);

        ///////////////////////////////////////////////////////////////////////////
        // Now process each flower in turn.
        ///////////////////////////////////////////////////////////////////////////
        FILE *fileHandle = NULL;
        if(outputFile != NULL) {
            fileHandle = fopen(outputFile, "w");
        }
        makeHalFormat(flower, sequenceDatabase, referenceEventName, fileHandle);
        if(fileHandle != NULL) {
            fclose(fileHandle);
        }

        // We aren't making any changes to the flower itself, only to
        // the secondary database. So there's no need to save the
        // flower here.
    }

    ///////////////////////////////////////////////////////////////////////////
    //Clean up memory
    ///////////////////////////////////////////////////////////////////////////

    cactusDisk_destruct(cactusDisk);

    return 0; //Exit without clean up is quicker, enable cleanup when doing memory leak detection.

    free(cactusDiskDatabaseString);
    free(secondaryDatabaseString);
    free(referenceEventString);
    free(logLevelString);

    st_logInfo("Cleaned stuff up and am finished\n");
    return 0;
}
