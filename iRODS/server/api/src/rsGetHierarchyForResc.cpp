/*** Copyright (c), The Unregents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/
/* unregDataObj.c
 */

#include "icatHighLevelRoutines.hpp"
#include "getHierarchyForResc.h"
#include "irods_stacktrace.hpp"

// =-=-=-=-=-=-=-
// local implementation which makes the icat high level
// call to compute the hierarchy for a given resource
int _rsGetHierarchyForResc(
    getHierarchyForRescInp_t*  _inp,
    getHierarchyForRescOut_t** _out ) {
    // =-=-=-=-=-=-=-
    // icat high level calls only work on a server
    // connected to the database
#ifdef RODS_CAT
    // =-=-=-=-=-=-=-
    // allocate the outgoing structure
    ( *_out ) = ( getHierarchyForRescOut_t* )malloc( sizeof( getHierarchyForRescOut_t ) );
    bzero( ( *_out ), sizeof( getHierarchyForRescOut_t ) );

    // =-=-=-=-=-=-=-
    // get local zone
    char* zone_name = getLocalZoneName();

    // =-=-=-=-=-=-=-
    // make the chl call to get the hierarchy
    std::string hier;
    int status = chlGetHierarchyForResc(
                     _inp->resc_name_,
                     zone_name,
                     hier );
    snprintf( ( *_out )->resc_hier_, MAX_NAME_LEN, "%s", hier.c_str() );

    return status;
#else
    return SYS_NO_RCAT_SERVER_ERR;
#endif

} // _rsGetHierarchyForResc

// =-=-=-=-=-=-=-
// connect to the icat server and issue the
// api call, if we are on the icat server run
// the icat highlevel function to compute the
// heirarchy of a resource in a composition (tree)
// starting at the root to the given resource
int rsGetHierarchyForResc(
    rsComm_t*                  _comm,
    getHierarchyForRescInp_t*  _inp,
    getHierarchyForRescOut_t** _out ) {
    // =-=-=-=-=-=-=-
    // get local zone
    char* zone_name = getLocalZoneName();

    // =-=-=-=-=-=-=-
    // use zone as hint to get the icat connectoin
    rodsServerHost_t* svr_host = NULL;
    int status = getAndConnRcatHost(
                     _comm,
                     MASTER_RCAT,
                     ( const char* )zone_name,
                     &svr_host );
    if ( status < 0 || NULL == svr_host ) {
        return status;
    }

    // =-=-=-=-=-=-=-
    // if we are on the icat server run the fetch,
    // otherwise redirect to the icat server
    if ( svr_host->localFlag == LOCAL_HOST ) {
#ifdef RODS_CAT
        status = _rsGetHierarchyForResc( _inp, _out );
#else
        status = SYS_NO_RCAT_SERVER_ERR;
#endif
    }
    else {
        status = rcGetHierarchyForResc( svr_host->conn, _inp, _out );
    }

    return status;

} // rsGetHierarchyForResc



