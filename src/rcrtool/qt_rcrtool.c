#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <qthread/qthread.h>           // for syncvar_t
#include "qthread_asserts.h"
#include "qt_atomics.h"
#include <err.h>
#include "common_rcrtool.h"
#include "qt_rcrtool.h"
#include "stdio.h"
#include "qthread_innards.h"

pthread_t rcrToolPThreadID;

RCRHashEntry hashTable[RCR_HASH_TABLE_SIZE];
int RCRParallelSectionStack[RCR_STACK_SIZE];
int RCRParallelSectionStackPos = -1;
char RCRAppName[RCR_APP_NAME_MAX_SIZE];

ShepWorkerInfo swinfo;

void *rcrtoolDaemon(void *swinfo) {
    if (pfm_initialize() != PFM_SUCCESS)
        errx(1, "libpfm initialization failed");
    //empty the hash table
    int i = 0;
    for (; i < RCR_HASH_TABLE_SIZE; i++) {
        hashTable[i].funcName[0] = 0;
    }
	buildTriggerMap("triggers.config");
    doWork(((ShepWorkerInfo*)swinfo)->nshepherds, ((ShepWorkerInfo*)swinfo)->nworkerspershep);
    return 0;
}

qt_rcrtool_level rcrtoollevel = 0;
QTHREAD_FASTLOCK_TYPE rcrtool_lock;

int qthread_rcrtoollevel(int d);
int qthread_rcrtoollevel(int d) {
    if (d >= 0) rcrtoollevel = d;
    return rcrtoollevel;
}

/*!
 * 
 * 
 * \param appName 
 * 
 * \return int 0 on success.
 */
static int storeRCRAppEntry (const char* appName) {
    strncpy(RCRAppName, appName, RCR_APP_NAME_MAX_SIZE-1);
    return 0;
}



/* for RCRTool logging */

extern qt_rcrtool_level rcrtoollevel;

/*!
 * Simple hash function based on djb2 function by Dan Bernstein.  Altered to 
 * avoid compiler warnings. 
 * 
 * \param str Sting key.
 * 
 * \return unsigned int Hash code in range of 0 to RCR_HASH_TABLE_SIZE - 1.
 */
static unsigned int hash(const unsigned char *str) {
    unsigned long hash = 5381;

    for (; *str; str++)
        hash = ((hash << 5) + hash) + *str; /* hash * 33 + c */

    return(unsigned int)(hash % RCR_HASH_TABLE_SIZE);
}

static int getHashRecord(const char* funcName) {
    unsigned int hashCode = hash((const unsigned char*)funcName);
    unsigned int hashCode2 = hashCode;
    for (; hashCode < RCR_HASH_TABLE_SIZE; hashCode++) {
        if (hashTable[hashCode].funcName[0] == 0) {
            hashTable[hashCode].count = 0;
            strncpy(hashTable[hashCode].funcName, funcName, RCR_HASH_ENTRY_SIZE - 1);
            return hashCode;
        }
        if (strncmp(hashTable[hashCode].funcName, funcName, RCR_HASH_ENTRY_SIZE - 1) == 0) {
            return hashCode;
        }
    }
    for (hashCode = 0; hashCode < hashCode2; hashCode++) {
        if (hashTable[hashCode].funcName[0] == 0) {
            hashTable[hashCode].count = 0;
            strncpy(hashTable[hashCode].funcName, funcName, RCR_HASH_ENTRY_SIZE - 1);
            return hashCode;
        }
        if (strncmp(hashTable[hashCode].funcName, funcName, RCR_HASH_ENTRY_SIZE - 1) == 0) {
            return hashCode;
        }
    }
    return RCR_HASH_LOOKUP_FAILURE;
}

static void RCREnterParallelSection(uint64_t data, const char* funcName) {
    int hashRecord = getHashRecord(funcName);
    if (hashRecord > RCR_HASH_LOOKUP_FAILURE) {
        hashTable[hashRecord].count++;
    }
    //push on stack even if failure
    RCRParallelSectionStackPos++;
    if (RCRParallelSectionStackPos < RCR_STACK_SIZE) {
        RCRParallelSectionStack[RCRParallelSectionStackPos] = hashRecord;
    }
}

static void RCRLeaveParallelSection(void) {
    //pop stack
    RCRParallelSectionStackPos--;
}

/*!
 * 
 * 
 * \param level 
 * \param type 
 * \param thread_id 
 * \param data 
 * \param data2 
 */
void rcrtool_log(qt_rcrtool_level level, XOMP_Type type, unsigned thread_id, uint64_t data, const char* data2) {

    int i, j; i = 234; j=2542;
    if (level <= rcrtoollevel || level == 0) {
        //Do some logging.
        switch (type) {
        case XOMP_PARALLEL_START:
            //printf("Entering parallel section %s.\n", data2);
            RCREnterParallelSection(data, data2);
            break;
        case XOMP_PARALLEL_END:
            //printf("Leaving parallel section.\n");
            RCRLeaveParallelSection();
            break;
        case XOMP_INIT:
            //Set-up debug file system entry.
            storeRCRAppEntry(data2);
            break;
        case XOMP_RAT_DEBUG:
            //use call to built-in debug system. Only allows printing one variable.
            //qthread_debug(0, data2, (int)data); 
            break;
        case XOMP_UNK:       
        case XOMP_TASK_START:
        case XOMP_TASKWAIT:
        case XOMP_TASK_END:
        case XOMP_FOR_LOOP_START:
        case XOMP_FOR_LOOP_END:
        case XOMP_BARRIER:
        case XOMP_STREAM_START:
        case XOMP_STREAM_END:
        case XOMP_TERMINATE:
        case XOMP_CRITICAL:
        case XOMP_SINGLE:
        default:
            break;
        }
    }
}
extern QTHREAD_FASTLOCK_TYPE rcrtool_lock;

