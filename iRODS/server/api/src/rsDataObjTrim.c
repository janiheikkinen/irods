/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/
/* See dataObjTrim.h for a description of this API call.*/

#include "dataObjTrim.h"
#include "dataObjUnlink.h"
#include "dataObjOpr.h"
#include "rodsLog.h"
#include "objMetaOpr.h"
#include "specColl.h"
#include "reGlobalsExtern.h"
#include "reDefines.h"
#include "reSysDataObjOpr.h"
#include "getRemoteZoneResc.h"

/* rsDataObjTrim - The Api handler of the rcDataObjTrim call - trim down 
 * the number of replica of a file
 * Input -
 *    rsComm_t *rsComm
 *    dataObjInp_t *dataObjInp - The trim input
 * 
 *  Returned val - return 1 if a copy is trimed. 0 if nothing trimed.
 */

int
rsDataObjTrim (rsComm_t *rsComm, dataObjInp_t *dataObjInp)
{
    rodsLog( LOG_NOTICE, "XXXX - rsDataObjTrim Start" );
    int status;
    dataObjInfo_t *dataObjInfoHead = NULL;
    dataObjInfo_t *tmpDataObjInfo;
    char *accessPerm;
    int retVal = 0;
    int remoteFlag;
    rodsServerHost_t *rodsServerHost;
    specCollCache_t *specCollCache = NULL;
    int myTime = 0;
    char *tmpStr;
    int myAge;

    resolveLinkedPath (rsComm, dataObjInp->objPath, &specCollCache,
      &dataObjInp->condInput);
    remoteFlag = getAndConnRemoteZone (rsComm, dataObjInp, &rodsServerHost,
      REMOTE_OPEN);

    if (remoteFlag < 0) {
        return (remoteFlag);
    } else if (remoteFlag == REMOTE_HOST) {
        status = rcDataObjTrim (rodsServerHost->conn, dataObjInp);
        return status;
    }

    if (getValByKey (&dataObjInp->condInput, IRODS_ADMIN_KW) != NULL) {
        if (rsComm->clientUser.authInfo.authFlag < LOCAL_PRIV_USER_AUTH) {
            return (CAT_INSUFFICIENT_PRIVILEGE_LEVEL);
        }
        accessPerm = NULL;
    } else {
        accessPerm = ACCESS_DELETE_OBJECT;
    }

    status = getDataObjInfo (rsComm, dataObjInp, &dataObjInfoHead,
      accessPerm, 1);

    if (status < 0) {
      rodsLog (LOG_ERROR,
          "rsDataObjTrim: getDataObjInfo for %s", dataObjInp->objPath);
        return (status);
    }
rodsLog( LOG_NOTICE, "XXXX - rsDataObjTrim :: calling resolveInfoForTrim" );
    status = resolveInfoForTrim (&dataObjInfoHead, &dataObjInp->condInput);
rodsLog( LOG_NOTICE, "XXXX - rsDataObjTrim :: calling resolveInfoForTrim. Done. status: %d", status );

    if (status < 0) {
	return (status);
    }

    if ((tmpStr = getValByKey (&dataObjInp->condInput, AGE_KW)) != NULL) {
        myAge = atoi (tmpStr);
	/* age value is in minutes */
        if (myAge > 0) myTime = time (0) - myAge * 60;
    }

rodsLog( LOG_NOTICE, "XXXX - rsDataObjTrim :: iterate!" );
    tmpDataObjInfo = dataObjInfoHead;
    while (tmpDataObjInfo != NULL) {
rodsLog( LOG_NOTICE, "XXXX - rsDataObjTrim :: iterate :: tmpDataObjInfo != NULL" );
    tmpDataObjInfo = dataObjInfoHead;
rodsLog( LOG_NOTICE, "XXXX - rsDataObjTrim :: iterate :: tmpDataObjInfo :: name %s  resc name %s resc hier %s", tmpDataObjInfo->objPath, tmpDataObjInfo->rescName, tmpDataObjInfo->rescHier );
        if (myTime == 0 || atoi (tmpDataObjInfo->dataModify) <= myTime) {
	    if (getValByKey (&dataObjInp->condInput, DRYRUN_KW) == NULL) {
rodsLog( LOG_NOTICE, "XXXX - rsDataObjTrim :: iterate :: call dataObjUnlinkS!" );
                status = dataObjUnlinkS (rsComm, dataObjInp, tmpDataObjInfo);
rodsLog( LOG_NOTICE, "XXXX - rsDataObjTrim :: iterate :: call dataObjUnlinkS Done. status: %d", status );
                if (status < 0) {
                    if (retVal == 0) {
                        retVal = status;
                    }
                } else {
	            retVal = 1;
	        }
	    } else {
		retVal = 1;
	    }
	}
        tmpDataObjInfo = tmpDataObjInfo->next;
    }
rodsLog( LOG_NOTICE, "XXXX - rsDataObjTrim :: iterate Done!" );

    freeAllDataObjInfo (dataObjInfoHead);

    rodsLog( LOG_NOTICE, "XXXX - rsDataObjTrim Start. Done." );
    return (retVal);
}


int
trimDataObjInfo (rsComm_t *rsComm, dataObjInfo_t *dataObjInfo) // JMC - backport 4537
{
    dataObjInp_t dataObjInp;
    char tmpStr[NAME_LEN];
    int status;

    bzero (&dataObjInp, sizeof (dataObjInp));
    rstrcpy (dataObjInp.objPath,  dataObjInfo->objPath, MAX_NAME_LEN);
    snprintf (tmpStr, NAME_LEN, "1");
    addKeyVal (&dataObjInp.condInput, COPIES_KW, tmpStr);
    addKeyVal (&dataObjInp.condInput, RESC_NAME_KW,
      dataObjInfo->rescInfo->rescName);
    status = rsDataObjTrim (rsComm, &dataObjInp);
    clearKeyVal (&dataObjInp.condInput);
    if (status < 0) {
        rodsLogError (LOG_ERROR, status,
          "trimDataObjInfo: rsDataObjTrim of %s error", dataObjInfo->objPath);
    }
    return status;
}

