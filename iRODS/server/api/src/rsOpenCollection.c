/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/

/* rsOpenCollection.c - server handling routine for rcOpenCollection
 */


#include "openCollection.h"
#include "closeCollection.h"
#include "rsGlobalExtern.h"
#include "rcGlobalExtern.h"

int
rsOpenCollection (rsComm_t *rsComm, collInp_t *openCollInp)
{
    rodsLog( LOG_NOTICE, "XXXX - rsOpenCollection :: Start rsComm %d, openCollInp %d", rsComm, openCollInp );
    int status;
    int handleInx;
    collHandle_t *collHandle;
    rodsObjStat_t *rodsObjStatOut = NULL;

    handleInx = allocCollHandle ();

    if (handleInx < 0) return (handleInx);

    collHandle = &CollHandle[handleInx];

    rodsLog( LOG_NOTICE, "XXXX - rsOpenCollection :: rsInitQueryHandle" );
    status = rsInitQueryHandle (&collHandle->queryHandle, rsComm);
    rodsLog( LOG_NOTICE, "XXXX - rsOpenCollection :: rsInitQueryHandle. done." );
   
    if (status < 0) return status;

    rstrcpy (collHandle->dataObjInp.objPath, openCollInp->collName, MAX_NAME_LEN);
      
    if ((openCollInp->flags & INCLUDE_CONDINPUT_IN_QUERY) != 0) {
	    replKeyVal (&openCollInp->condInput, &collHandle->dataObjInp.condInput);
    }

    rodsLog( LOG_NOTICE, "XXXX - rsOpenCollection :: rsObjStat" );
    status = rsObjStat (rsComm, &collHandle->dataObjInp, &rodsObjStatOut);
    rodsLog( LOG_NOTICE, "XXXX - rsOpenCollection :: rsObjStat. done." );
    if (status < 0) {
	    rsCloseCollection (rsComm, &handleInx);
    rodsLog( LOG_NOTICE, "XXXX - rsOpenCollection :: early exit 1" );
	    return status;
    }

    if (rodsObjStatOut->objType != COLL_OBJ_T) {
        freeRodsObjStat (rodsObjStatOut);
        rsCloseCollection (rsComm, &handleInx);
    rodsLog( LOG_NOTICE, "XXXX - rsOpenCollection :: early exit 2" );
        return CAT_NAME_EXISTS_AS_DATAOBJ;
    }

#if 0
    collHandle->dataObjInp.specColl = rodsObjStatOut->specColl;
#else
    rodsLog( LOG_NOTICE, "XXXX - rsOpenCollection :: replSpecColl" );
    replSpecColl (rodsObjStatOut->specColl, &collHandle->dataObjInp.specColl);
    rodsLog( LOG_NOTICE, "XXXX - rsOpenCollection :: replSpecColl. done." );
#endif
    if( rodsObjStatOut->specColl != NULL && 
        rodsObjStatOut->specColl->collClass == LINKED_COLL) {
        /* save the linked path */
        rstrcpy( collHandle->linkedObjPath, rodsObjStatOut->specColl->objPath, MAX_NAME_LEN );
    };

    collHandle->rodsObjStat = rodsObjStatOut;

    collHandle->state = COLL_OPENED;
    collHandle->flags = openCollInp->flags;
    rodsLog( LOG_NOTICE, "XXXX - rsOpenCollection :: Done." );
    /* the collection exist. now query the data in it */
    return (handleInx);
}

#ifdef COMPAT_201
int
rsOpenCollection201 (rsComm_t *rsComm, openCollInp_t *openCollInp)
{
    collInp_t collInp;
    int status;

    bzero (&collInp, sizeof (collInp));

    rstrcpy (collInp.collName, openCollInp->collName, MAX_NAME_LEN);
    collInp.flags = openCollInp->flags;
    collInp.condInput = openCollInp->condInput;

    status = rsOpenCollection (rsComm, &collInp);

    return status;
}
#endif

