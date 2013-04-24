


// =-=-=-=-=-=-=-
// irods includes
#include "msParam.h"
#include "reGlobalsExtern.h"
#include "generalAdmin.h"
#include "physPath.h"
#include "reIn2p3SysRule.h"

// =-=-=-=-=-=-=-
// eirods includes
#include "eirods_resource_plugin.h"
#include "eirods_file_object.h"
#include "eirods_physical_object.h"
#include "eirods_collection_object.h"
#include "eirods_string_tokenize.h"
#include "eirods_hierarchy_parser.h"
#include "eirods_resource_redirect.h"
#include "eirods_stacktrace.h"

// =-=-=-=-=-=-=-
// stl includes
#include <iostream>
#include <sstream>
#include <vector>
#include <string>

// =-=-=-=-=-=-=-
// boost includes
#include <boost/lexical_cast.hpp>
#include <boost/function.hpp>
#include <boost/any.hpp>


extern "C" {

    #define NB_READ_TOUT_SEC        60      /* 60 sec timeout */
    #define NB_WRITE_TOUT_SEC       60      /* 60 sec timeout */

    // =-=-=-=-=-=-=-
    // constants for property map flags to signal a sync or stage
    const std::string SYNC_FLAG  ( "sync_flag" );   // property map key for sync to arch
    const std::string SYNC_UPDATE( "sync_update" ); // property map value for sync with update
    const std::string SYNC_CREATE( "sync_create" ); // property map value for sync with create
    const std::string SYNC_NONE  ( "sync_none" );   // property map value for no operation ( ex: open with read )

    // =-=-=-=-=-=-=-
    // 1. Define plugin Version Variable, used in plugin
    //    creation when the factory function is called.
    //    -- currently only 1.0 is supported.
    double EIRODS_PLUGIN_INTERFACE_VERSION=1.0;
 
    // =-=-=-=-=-=-=-
    /// @ brief constant to index the cache child resource
    const std::string CACHE_CONTEXT_TYPE( "cache" );

    // =-=-=-=-=-=-=-
    /// @ brief constant to index the archive child resource
    const std::string ARCHIVE_CONTEXT_TYPE( "archive" );

    // =-=-=-=-=-=-=-
    /// @brief helper function to take a rule result, find a keyword and then
    ///        parse it for the value
    eirods::error get_stage_policy(
        const std::string& _results,
        std::string& _policy ) {
        // =-=-=-=-=-=-=-
        // tokenize the string based on the key/value pair delim
        std::vector< std::string > toks;
        eirods::string_tokenize( _results, ";", toks );

        // =-=-=-=-=-=-=-
        // iterate over the pairs, find the stage policy
        std::string policy;
        for( size_t i = 0; i < toks.size(); ++i ) {
            // =-=-=-=-=-=-=-
            // find the policy key in the string
            size_t pos = _results.find( eirods::RESOURCE_STAGE_TO_CACHE_POLICY );
            if( std::string::npos != pos ) {
                policy = toks[ i ].substr( pos+1 ); 
                break;
            }
        }

        return SUCCESS();

    } // get_stage_policy

    // =-=-=-=-=-=-=-
    /// @brief start up operation - determine which child is the cache and which is the
    ///        archive.  cache those names in local variables for ease of use
    eirods::error compound_start_operation( 
                  eirods::resource_property_map& _prop_map,
                  eirods::resource_child_map&    _cmap ) {
        // =-=-=-=-=-=-=-
        // trap invalid number of children 
        if( _cmap.size() == 0 || _cmap.size() > 2 ) {
            std::stringstream msg;
            msg << "compound resource: invalid number of children [";
            msg << _cmap.size() << "]";
            return ERROR( SYS_INVALID_INPUT_PARAM, msg.str() );
        }

        // =-=-=-=-=-=-=-
        // child map is indexed by name, get first name
        std::string first_child_name;
        eirods::resource_child_map::iterator itr = _cmap.begin();
        if( itr == _cmap.end() ) {
            return ERROR( -1, "child map is empty" );
        }
        
        std::string           first_child_ctx  = itr->second.first;
        eirods::resource_ptr& first_child_resc = itr->second.second;
        eirods::error get_err = first_child_resc->get_property<std::string>( "name", first_child_name );
        if( !get_err.ok() ) {
            return PASS( get_err );
        }

        // =-=-=-=-=-=-=-
        // get second name
        std::string second_child_name;
        itr++;
        if( itr == _cmap.end() ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "child map has only one entry" );
        }

        std::string          second_child_ctx  = itr->second.first;
        eirods::resource_ptr second_child_resc = itr->second.second;
        get_err = second_child_resc->get_property<std::string>( "name", second_child_name );
        if( !get_err.ok() ) {
            return PASS( get_err );
        }
        
        // =-=-=-=-=-=-=-
        // now if first name is a cache, store that name as such
        // otherwise it is the archive
        // cmap is a hash map whose payload is a pair< string, resource_ptr >
        // the first of which is the parent-child context string denoting 
        // that either the child is a cache or archive
        if( first_child_ctx == CACHE_CONTEXT_TYPE ) {
            _prop_map[ CACHE_CONTEXT_TYPE ] = first_child_name;

        } else if( first_child_ctx == ARCHIVE_CONTEXT_TYPE ) {
            _prop_map[ ARCHIVE_CONTEXT_TYPE ] = first_child_name;

        } else {
            return ERROR( EIRODS_INVALID_RESC_CHILD_CONTEXT, first_child_ctx );

        }

        // =-=-=-=-=-=-=-
        // do the same for the second resource
        if( second_child_ctx == CACHE_CONTEXT_TYPE ) {
            _prop_map[ CACHE_CONTEXT_TYPE ] = second_child_name;

        } else if( second_child_ctx == ARCHIVE_CONTEXT_TYPE ) {
            _prop_map[ ARCHIVE_CONTEXT_TYPE ] = second_child_name;

        } else {
            return ERROR( EIRODS_INVALID_RESC_CHILD_CONTEXT, second_child_ctx );

        }

        // =-=-=-=-=-=-=-
        // if both context strings match, this is also an error
        if( first_child_ctx == second_child_ctx ) {
            std::stringstream msg;
            msg << "matching child context strings :: ";
            msg << "[" << first_child_ctx  << "] vs ";
            msg << "[" << second_child_ctx << "]";
            return ERROR( EIRODS_INVALID_RESC_CHILD_CONTEXT, msg.str() );

        }

        // =-=-=-=-=-=-=-
        // and... were done.
        return SUCCESS();

    } // compound_start_operation

    /// =-=-=-=-=-=-=-
    /// @brief Check the general parameters passed in to most plugin functions
    inline eirods::error compound_check_param(
        eirods::resource_operation_context* _ctx ) { 
        // =-=-=-=-=-=-=-
        // check the resource context
        if( !_ctx ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "resource context is null" );
        }

        // =-=-=-=-=-=-=-
        // ask the context if it is valid
        eirods::error ret = _ctx->valid();
        if( !ret.ok() ) {
            return PASSMSG( "resource context is invalid", ret );

        }
       
        return SUCCESS();
 
    } // compound_check_param

    // =-=-=-=-=-=-=-
    /// @brief helper function to get the cache resource 
    eirods::error get_cache( 
        eirods::resource_operation_context* _ctx,
        eirods::resource_ptr&               _resc ) {
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache name
        std::string resc_name;
        ret = _ctx->prop_map().get< std::string >( CACHE_CONTEXT_TYPE, resc_name );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // extract the resource from the child map
        std::pair< std::string, eirods::resource_ptr > resc_pair;
        ret = _ctx->child_map().get( resc_name, resc_pair );
        if( !ret.ok() ) {
            std::stringstream msg;
            msg << "failed to get child resource [" << resc_name << "]";
            return PASSMSG( msg.str(), ret );
        }

        // =-=-=-=-=-=-=-
        // assign the resource to the out variable
        _resc = resc_pair.second;

        return SUCCESS();

    } // get_cache

    // =-=-=-=-=-=-=-
    /// @brief helper function to get the archive resource 
    eirods::error get_archive( 
        eirods::resource_operation_context* _ctx,
        eirods::resource_ptr&               _resc ) {
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the archive name
        std::string resc_name;
        ret = _ctx->prop_map().get< std::string >( ARCHIVE_CONTEXT_TYPE, resc_name );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // extract the resource from the child map
        std::pair< std::string, eirods::resource_ptr > resc_pair;
        ret = _ctx->child_map().get( resc_name, resc_pair );
        if( !ret.ok() ) {
            std::stringstream msg;
            msg << "failed to get child resource [" << resc_name << "]";
            return PASSMSG( msg.str(), ret );
        }

        // =-=-=-=-=-=-=-
        // assign the resource to the out variable
        _resc = resc_pair.second;
        return SUCCESS();

    } // get_archive

    /// =-=-=-=-=-=-=-
    /// @brief replicate a given object for either a sync or a stage
    eirods::error repl_object(
        eirods::resource_operation_context* _ctx,
        const char*                         _stage_sync_kw,
        bool                                _update_flg ) {
        // =-=-=-=-=-=-=-
        // error check incoming params
        if( !_ctx ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "null resource context" );
        }

        if( !_stage_sync_kw || strlen(_stage_sync_kw) == 0 ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "null or empty _stage_sync_kw" );

        }
        
        // =-=-=-=-=-=-=-
        // get the root resource to pass to the cond input
        eirods::hierarchy_parser parser;
        parser.set_string( _ctx->fco().resc_hier() );

        std::string resource;
        parser.first_resc( resource );
        
        // =-=-=-=-=-=-=-
        // get the cache name
        std::string cache_name;
        eirods::error ret = _ctx->prop_map().get< std::string >( CACHE_CONTEXT_TYPE, cache_name );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // get the archive name
        std::string arch_name;
        ret = _ctx->prop_map().get< std::string >( ARCHIVE_CONTEXT_TYPE, arch_name );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // manufacture a resc hier to the archive resc
        std::string src_hier = _ctx->fco().resc_hier();
        size_t pos = src_hier.find( cache_name );
        std::string dst_hier = src_hier.substr( pos );
        dst_hier += ";" + arch_name;


        // =-=-=-=-=-=-=-
        // create a data obj input struct to call rsDataObjRepl which given
        // the _stage_sync_kw will either stage or sync the data object 
        dataObjInp_t data_obj_inp;
        bzero( &data_obj_inp, sizeof( data_obj_inp ) );
        rstrcpy( data_obj_inp.objPath, _ctx->fco().logical_path().c_str(), MAX_NAME_LEN );
        data_obj_inp.createMode = _ctx->fco().mode();
        addKeyVal( &data_obj_inp.condInput, RESC_HIER_STR_KW, _ctx->fco().resc_hier().c_str() );
        addKeyVal( &data_obj_inp.condInput, DEST_RESC_HIER_STR_KW, dst_hier.c_str() );
        addKeyVal( &data_obj_inp.condInput, RESC_NAME_KW, resource.c_str() );
        addKeyVal( &data_obj_inp.condInput, DEST_RESC_NAME_KW, resource.c_str() );
        addKeyVal( &data_obj_inp.condInput, IN_PDMO_KW, "" );
        addKeyVal( &data_obj_inp.condInput, _stage_sync_kw, "" );
        if( _update_flg ) {
            addKeyVal( &data_obj_inp.condInput, UPDATE_REPL_KW, "" );
        }

        transferStat_t* trans_stat = NULL;
        int status = rsDataObjRepl( _ctx->comm(), &data_obj_inp, &trans_stat );
        if( status < 0 ) {
            char* sys_error;
            char* rods_error = rodsErrorName(status, &sys_error);
            std::stringstream msg;
            msg << "Failed to replicate the data object [" << _ctx->fco().logical_path() << "] ";
            msg << "for operation [" << _stage_sync_kw << "]";
            return ERROR( status, msg.str() );
        }

        return SUCCESS();

    } // repl_object

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX create
    eirods::error compound_file_create( 
        eirods::resource_operation_context* _ctx ) { 
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call( _ctx->comm(), "create", _ctx->fco() );

    } // compound_file_create

    // =-=-=-=-=-=-=-
    // interface for POSIX Open
    eirods::error compound_file_open( 
        eirods::resource_operation_context* _ctx ) { 
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call( _ctx->comm(), "open", _ctx->fco() );

    } // compound_file_open

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX Read
    eirods::error compound_file_read(
        eirods::resource_operation_context* _ctx,
        void*                               _buf, 
        int                                 _len ) {
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call< void*, int >( _ctx->comm(), "read", _ctx->fco(), _buf, _len );

    } // compound_file_read

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX Write
    eirods::error compound_file_write( 
        eirods::resource_operation_context* _ctx,
        void*                               _buf, 
        int                                 _len ) {
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call< void*, int >( _ctx->comm(), "write", _ctx->fco(), _buf, _len );
 
    } // compound_file_write

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX Close
    eirods::error compound_file_close(
        eirods::resource_operation_context* _ctx ) { 
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        ret = resc->call( _ctx->comm(), "close", _ctx->fco() );
        if( ret.ok() ) {
            // =-=-=-=-=-=-=-
            // if all went well check to see if we need to sync to arch
            // we use a flag set on resolve_resource_hierarchy for open
            // which implies an exisitng file need be updated
            std::string flag;
            ret = _ctx->prop_map().get< std::string >( SYNC_FLAG, flag );
            if( ret.ok() ) {
                if( SYNC_UPDATE == flag ) {
                    // =-=-=-=-=-=-=-
                    // get the existing hier
                    return repl_object( _ctx, SYNC_OBJ_KW, true );

                } else {
                    std::stringstream msg;
                    msg << "sync flag is not sync_update [" << flag << "]";
                    eirods::log( ERROR( -1, msg.str() ) );

                }

            } // if ret.ok

        } else { 
            return PASS( ret );
        
        }

        return SUCCESS();
          
    } // compound_file_close

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX Unlink
    eirods::error compound_file_unlink(
        eirods::resource_operation_context* _ctx ) { 
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call( _ctx->comm(), "unlink", _ctx->fco() );

    } // compound_file_unlink

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX Stat
    eirods::error compound_file_stat(
        eirods::resource_operation_context* _ctx,
        struct stat*                        _statbuf ) {
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call< struct stat* >( _ctx->comm(), "stat", _ctx->fco(), _statbuf );

    } // compound_file_stat

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX Fstat
    eirods::error compound_file_fstat(
        eirods::resource_operation_context* _ctx,
        struct stat*                        _statbuf ) {
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call< struct stat* >( _ctx->comm(), "fstat", _ctx->fco(), _statbuf );
 
    } // compound_file_fstat

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX lseek
    eirods::error compound_file_lseek(
        eirods::resource_operation_context* _ctx,
        size_t                              _offset, 
        int                                 _whence ) {
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call< size_t, int >( _ctx->comm(), "lseek", _ctx->fco(), _offset, _whence );
 
    } // compound_file_lseek

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX fsync
    eirods::error compound_file_fsync(
        eirods::resource_operation_context* _ctx ) { 
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call( _ctx->comm(), "fsync", _ctx->fco() );
 
    } // compound_file_fsync

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX chmod
    eirods::error compound_file_chmod(
        eirods::resource_operation_context* _ctx ) { 
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call( _ctx->comm(), "chmod", _ctx->fco() );

    } // compound_file_chmod

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX mkdir
    eirods::error compound_file_mkdir(
        eirods::resource_operation_context* _ctx ) { 
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call( _ctx->comm(), "mkdir", _ctx->fco() );

    } // compound_file_mkdir

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX rmdir
    eirods::error compound_file_rmdir(
        eirods::resource_operation_context* _ctx ) { 
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call( _ctx->comm(), "rmdir", _ctx->fco() );

    } // compound_file_rmdir

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX opendir
    eirods::error compound_file_opendir(
        eirods::resource_operation_context* _ctx ) { 
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call( _ctx->comm(), "opendir", _ctx->fco() );

    } // compound_file_opendir

    // =-=-=-=-=-=-=-
    /// @brief interface for POSIX closedir
    eirods::error compound_file_closedir(
        eirods::resource_operation_context* _ctx ) { 
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call( _ctx->comm(), "closedir", _ctx->fco() );

    } // compound_file_closedir

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX readdir
    eirods::error compound_file_readdir(
        eirods::resource_operation_context* _ctx,
        struct rodsDirent**                 _dirent_ptr ) {
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call< struct rodsDirent** >( _ctx->comm(), "readdir", _ctx->fco(), _dirent_ptr );

    } // compound_file_readdir

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX file_stage
    eirods::error compound_file_stage(
        eirods::resource_operation_context* _ctx ) { 
        return ERROR( SYS_NOT_SUPPORTED, __FUNCTION__ );

    } // compound_file_stage

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX rename
    eirods::error compound_file_rename(
        eirods::resource_operation_context* _ctx,
        const char*                         _new_file_name ) {
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call<const char*>( _ctx->comm(), "rename", _ctx->fco(), _new_file_name );

    } // compound_file_rename

    /// =-=-=-=-=-=-=-
    /// @brief interface for POSIX truncate
    eirods::error compound_file_truncate(
        eirods::resource_operation_context* _ctx ) { 
        // =-=-=-=-=-=-=-
        // check the context for validity
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            return PASSMSG( "invalid resource context", ret);
        }

        // =-=-=-=-=-=-=-
        // get the cache child resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call
        return resc->call( _ctx->comm(), "truncate", _ctx->fco() );

    } // compound_file_truncate

    /// =-=-=-=-=-=-=-
    /// @brief interface to determine free space on a device given a path
    eirods::error compound_file_getfs_freespace(
        eirods::resource_operation_context* _ctx ) { 

    } // compound_file_getfs_freespace


    /// =-=-=-=-=-=-=-
    /// @brief Move data from a the archive leaf node to a local cache leaf node
    ///        This routine is not supported from a coordinating node's perspective
    ///        the coordinating node would be calling this on a leaf when necessary
    eirods::error compound_file_stage_to_cache(
        eirods::resource_operation_context* _ctx,
        const char*                         _cache_file_name ) { 
        // Check the operation parameters and update the physical path
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            std::stringstream msg;
            msg << "Invalid parameters or physical path.";
            return PASSMSG(msg.str(), ret);
        }
      
        // =-=-=-=-=-=-=-
        // get the archive resource
        eirods::resource_ptr resc;
        ret = get_archive( _ctx, resc ); 
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call to the archive
        return resc->call< const char* >( _ctx->comm(), "stagetocache", _ctx->fco(), _cache_file_name );

    } // compound_file_stage_to_cache

    /// =-=-=-=-=-=-=-
    /// @brief Move data from a the cache leaf node to a local archive leaf node
    ///        This routine is not supported from a coordinating node's perspective
    ///        the coordinating node would be calling this on a leaf when necessary
    eirods::error compound_file_sync_to_arch(
        eirods::resource_operation_context* _ctx, 
        const char*                         _cache_file_name ) { 
        // Check the operation parameters and update the physical path
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            std::stringstream msg;
            msg << "Invalid parameters or physical path.";
            return PASSMSG(msg.str(), ret);
        }
      
        // =-=-=-=-=-=-=-
        // get the archive resource
        eirods::resource_ptr resc;
        get_archive( _ctx, resc ); 
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // forward the call to the archive
        return resc->call< const char* >( _ctx->comm(), "synctoarch", _ctx->fco(), _cache_file_name );

    } // compound_file_sync_to_arch

    /// =-=-=-=-=-=-=-
    /// @brief interface to notify of a file registration
    eirods::error compound_file_registered(
        eirods::resource_operation_context* _ctx ) {
        rodsLog( LOG_NOTICE, "XXXX - compound_file_registered" );
        // Check the operation parameters and update the physical path
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            std::stringstream msg;
            msg << "Invalid parameters or physical path.";
            return PASSMSG(msg.str(), ret);
        }

        std::string flag;
        ret = _ctx->prop_map().get< std::string >( SYNC_FLAG, flag );
        rodsLog( LOG_NOTICE, "XXXX - compound_file_registered :: sync flag [%s]", flag.c_str() );
        if( ret.ok() ) {
            if( SYNC_CREATE == flag ) {
                return repl_object( _ctx, SYNC_OBJ_KW, false );

            } else {
                std::stringstream msg;
                msg << "sync flag is not sync_create [" << flag << "]";
                eirods::log( ERROR( -1, msg.str() ) );

            }

        } // if ret.ok
        
        return SUCCESS();

    } // compound_file_registered
    
    /// =-=-=-=-=-=-=-
    /// @brief interface to notify of a file unregistration
    eirods::error compound_file_unregistered(
        eirods::resource_operation_context* _ctx) {
        // Check the operation parameters and update the physical path
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            std::stringstream msg;
            msg << "Invalid parameters or physical path.";
            return PASSMSG(msg.str(), ret);
        }
        // NOOP
        return SUCCESS();

    } // compound_file_unregistered
    
    /// =-=-=-=-=-=-=-
    /// @brief interface to notify of a file modification
    eirods::error compound_file_modified(
        eirods::resource_operation_context* _ctx) {
        // Check the operation parameters and update the physical path
        eirods::error ret = compound_check_param(_ctx);
        if(!ret.ok()) {
            std::stringstream msg;
            msg << "Invalid parameters or physical path.";
            return PASSMSG(msg.str(), ret);
        }
        // NOOP
        return SUCCESS();
    
    } // compound_file_modified

    // =-=-=-=-=-=-=-
    // redirect_get - code to determine redirection for get operation
    eirods::error compound_file_redirect_create( 
        eirods::resource_operation_context* _ctx,
        const std::string&             _resc_name, 
        const std::string*             _curr_host, 
        eirods::hierarchy_parser*      _out_parser,
        float*                         _out_vote ) {
        // =-=-=-=-=-=-=-
        // determine if the resource is down 
        int resc_status = 0;
        eirods::error ret = _ctx->prop_map().get< int >( "status", resc_status );
        if( !ret.ok() ) {
            return PASSMSG( "failed to get 'status' property", ret );
        }

        // =-=-=-=-=-=-=-
        // if the status is down, vote no.
        if( INT_RESC_STATUS_DOWN == resc_status ) {
            (*_out_vote ) = 0.0;
            return SUCCESS(); 
        }

        // =-=-=-=-=-=-=-
        // get the cache resource
        eirods::resource_ptr resc;
        ret = get_cache( _ctx, resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }
        
        // =-=-=-=-=-=-=-
        // set the sync flag in the prop map
        _ctx->prop_map().set( SYNC_FLAG, SYNC_CREATE );

        // =-=-=-=-=-=-=-
        // ask the cache if it is willing to accept a new file, politely
        ret = resc->call< const std::string*, const std::string*, 
                          eirods::hierarchy_parser*, float* >( 
                          _ctx->comm(), "redirect", _ctx->fco(), 
                          &eirods::EIRODS_CREATE_OPERATION, _curr_host, 
                          _out_parser, _out_vote );
        return ret;

    } // compound_file_redirect_create

    // =-=-=-=-=-=-=-
    /// @brief - handler for prefer archive policy
    eirods::error open_for_prefer_archive_policy( 
        eirods::resource_operation_context* _ctx,
        const std::string*                  _curr_host, 
        eirods::hierarchy_parser*           _out_parser,
        float*                              _out_vote ) {
        // =-=-=-=-=-=-=-
        // check incoming parameters
        if( !_curr_host ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "null operation" );
        }
        if( !_out_parser ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "null outgoing hier parser" );
        }
        if( !_out_vote ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "null outgoing vote" );
        }
 
        // =-=-=-=-=-=-=-
        // get the cache resource
        eirods::resource_ptr cache_resc;
        eirods::error ret = get_cache( _ctx, cache_resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // ask the cache if it has the data object in question, politely
        float                    cache_check_vote   = 0.0;
        eirods::hierarchy_parser cache_check_parser = (*_out_parser);
        ret = cache_resc->call< const std::string*, const std::string*, 
                                eirods::hierarchy_parser*, float* >( 
                                _ctx->comm(), "redirect", _ctx->fco(), 
                                &eirods::EIRODS_OPEN_OPERATION, _curr_host, 
                                &cache_check_parser, &cache_check_vote );
        // =-=-=-=-=-=-=-
        // if the vote is 0 then we do a wholesale stage, not an update
        // otherwise it is an update operation for the stage to cache
        bool update_flg = ( 0.0 == cache_check_vote );
        ret = repl_object( _ctx, STAGE_OBJ_KW, update_flg );
        if( !ret.ok() ) {
            return PASS( ret );    
        }

        // =-=-=-=-=-=-=-
        // now that the file is staged we will once again get the vote 
        // from the resouce hierarchy
        ret = cache_resc->call< const std::string*, const std::string*, 
                                eirods::hierarchy_parser*, float* >( 
                                _ctx->comm(), "redirect", _ctx->fco(), 
                                &eirods::EIRODS_OPEN_OPERATION, _curr_host, 
                                _out_parser, _out_vote );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        return SUCCESS();

    } // open_for_prefer_archive_policy

    // =-=-=-=-=-=-=-
    /// @brief - handler for prefer cache policy
    eirods::error open_for_prefer_cache_policy( 
        eirods::resource_operation_context* _ctx,
        const std::string*                  _curr_host, 
        eirods::hierarchy_parser*           _out_parser,
        float*                              _out_vote ) {
        // =-=-=-=-=-=-=-
        // check incoming parameters
        if( !_curr_host ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "null operation" );
        }
        if( !_out_parser ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "null outgoing hier parser" );
        }
        if( !_out_vote ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "null outgoing vote" );
        }
         
        // =-=-=-=-=-=-=-
        // get the cache resource
        eirods::resource_ptr cache_resc;
        eirods::error ret = get_cache( _ctx, cache_resc );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // ask the cache if it has the data object in question, politely
        float                    cache_check_vote   = 0.0;
        eirods::hierarchy_parser cache_check_parser = (*_out_parser);
        ret = cache_resc->call< const std::string*, const std::string*, 
                                eirods::hierarchy_parser*, float* >( 
                                _ctx->comm(), "redirect", _ctx->fco(), 
                                &eirods::EIRODS_OPEN_OPERATION, _curr_host, 
                                &cache_check_parser, &cache_check_vote );
        // =-=-=-=-=-=-=-
        // if the vote is 0 then the cache doesnt have it so it will need be staged
        if( 0.0 == cache_check_vote ) {
            // NOTE:: this invokes another redirect call to the cache, but that shouldn't matter
            return open_for_prefer_archive_policy( _ctx, _curr_host, _out_parser, _out_vote );

        } 
        // else it is in the cache so assign the parser 
        else {
            (*_out_vote)   = cache_check_vote;
            (*_out_parser) = cache_check_parser; 
        }

        return SUCCESS();

    } // open_for_prefer_cache_policy

    // =-=-=-=-=-=-=-
    /// @brief - code to determine redirection for the open operation
    ///          determine the user set policy for staging to cache
    ///          otherwise the default is to compare checsum
    eirods::error compound_file_redirect_open( 
        eirods::resource_operation_context* _ctx,
        const std::string*                  _curr_host, 
        eirods::hierarchy_parser*           _out_parser,
        float*                              _out_vote ) {
        // =-=-=-=-=-=-=-
        // check incoming parameters
        if( !_curr_host ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "null operation" );
        }
        if( !_out_parser ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "null outgoing hier parser" );
        }
        if( !_out_vote ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "null outgoing vote" );
        }

        // =-=-=-=-=-=-=-
        // determine if the resource is down 
        int resc_status = 0;
        eirods::error ret = _ctx->prop_map().get< int >( "status", resc_status );
        if( !ret.ok() ) {
            return PASS( ret );
        }

        // =-=-=-=-=-=-=-
        // if the status is down, vote no.
        if( INT_RESC_STATUS_DOWN == resc_status ) {
            (*_out_vote ) = 0.0;
            return SUCCESS(); 
        }
        
        // =-=-=-=-=-=-=-
        // set the default no op open flag in the property map
        _ctx->prop_map().set( SYNC_FLAG, SYNC_NONE );

        // =-=-=-=-=-=-=-
        // acquire the value of the stage policy from the results string
        std::string policy;
        ret = get_stage_policy( _ctx->rule_results(), policy );

        // =-=-=-=-=-=-=-
        // if the policy is prefer cache then if the cache has the object 
        // return an upvote
        if( policy.empty() || eirods::RESOURCE_STAGE_PREFER_CACHE == policy ) {
            return open_for_prefer_cache_policy( _ctx, _curr_host, _out_parser, _out_vote );
        } 
        // =-=-=-=-=-=-=-
        // if the policy is always, then if the archive has it 
        // stage from archive to cache and return an upvote
        else if( eirods::RESOURCE_STAGE_PREFER_ARCHIVE == policy ) {
            return open_for_prefer_archive_policy( _ctx, _curr_host, _out_parser, _out_vote );

        } else {
            std::stringstream msg;
            msg << "invalid stage policy [" << policy << "]"; 
            return ERROR( SYS_INVALID_INPUT_PARAM, msg.str() );
        }

        return SUCCESS();

    } // compound_file_redirect_open

    // =-=-=-=-=-=-=-
    // used to allow the resource to determine which host
    // should provide the requested operation
    eirods::error compound_file_redirect( 
        eirods::resource_operation_context* _ctx,
        const std::string*                  _opr,
        const std::string*                  _curr_host,
        eirods::hierarchy_parser*           _out_parser,
        float*                              _out_vote ) {

        // =-=-=-=-=-=-=-
        // check the context pointer
        if( !_ctx ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "invalid resource context" );
        }
         
        // =-=-=-=-=-=-=-
        // check the context validity
        eirods::error ret = _ctx->valid< eirods::file_object >(); 
        if(!ret.ok()) {
            std::stringstream msg;
            msg << "resource context is invalid";
            return PASSMSG( msg.str(), ret );
        }
 
        // =-=-=-=-=-=-=-
        // check incoming parameters
        if( !_opr ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "null operation" );
        }
        if( !_curr_host ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "null operation" );
        }
        if( !_out_parser ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "null outgoing hier parser" );
        }
        if( !_out_vote ) {
            return ERROR( SYS_INVALID_INPUT_PARAM, "null outgoing vote" );
        }
        
        // =-=-=-=-=-=-=-
        // cast down the chain to our understood object type
        eirods::file_object& file_obj = dynamic_cast< eirods::file_object& >( _ctx->fco() );

        // =-=-=-=-=-=-=-
        // get the name of this resource
        std::string resc_name;
        ret = _ctx->prop_map().get< std::string >( "name", resc_name );
        if( !ret.ok() ) {
            std::stringstream msg;
            msg << "failed in get property for name";
            return ERROR( SYS_INVALID_INPUT_PARAM, msg.str() );
        }

        // =-=-=-=-=-=-=-
        // add ourselves to the hierarchy parser by default
        _out_parser->add_child( resc_name );

        // =-=-=-=-=-=-=-
        // test the operation to determine which choices to make
        if( eirods::EIRODS_OPEN_OPERATION == (*_opr) ) {
            // =-=-=-=-=-=-=-
            // call redirect determination for 'get' operation
            return compound_file_redirect_open( _ctx, _curr_host, _out_parser, _out_vote );

        } else if( eirods::EIRODS_CREATE_OPERATION == (*_opr) ) {
            // =-=-=-=-=-=-=-
            // call redirect determination for 'create' operation
            return compound_file_redirect_create( _ctx, resc_name, _curr_host, _out_parser, _out_vote );

        }
      
        // =-=-=-=-=-=-=-
        // must have been passed a bad operation 
        std::stringstream msg;
        msg << "operation not supported [";
        msg << (*_opr) << "]";
        return ERROR( -1, msg.str() );

    } // compound_file_redirect

    // =-=-=-=-=-=-=-
    // 3. create derived class to handle universal mss resources
    //    context string will hold the script to be called.
    class compound_resource : public eirods::resource {
    public:
        compound_resource( const std::string& _inst_name, 
                           const std::string& _context ) : 
            eirods::resource( _inst_name, _context ) {
            // =-=-=-=-=-=-=-
            // set the start operation to identify the cache and archive children 
            set_start_operation( "compound_start_operation" );
        }

        // =-=-=-=-=-=-
        // override from plugin_base
        eirods::error need_post_disconnect_maintenance_operation( bool& _flg ) {
            _flg = false;
            return SUCCESS();
        }

        // =-=-=-=-=-=-
        // override from plugin_base
        eirods::error post_disconnect_maintenance_operation( eirods::pdmo_type& _pdmo ) {
            return ERROR( -1, "nop" );
        }

    }; // class compound_resource

    // =-=-=-=-=-=-=-
    // 4. create the plugin factory function which will return a dynamically
    //    instantiated object of the previously defined derived resource.  use
    //    the add_operation member to associate a 'call name' to the interfaces
    //    defined above.  for resource plugins these call names are standardized
    //    as used by the eirods facing interface defined in 
    //    server/drivers/src/fileDriver.c
    eirods::resource* plugin_factory( const std::string& _inst_name, 
                                      const std::string& _context  ) {
        // =-=-=-=-=-=-=-
        // 4a. create compound_resource object
        compound_resource* resc = new compound_resource( _inst_name, _context );

        // =-=-=-=-=-=-=-
        // 4b. map function names to operations.  this map will be used to load
        //     the symbols from the shared object in the delay_load stage of 
        //     plugin loading.
        resc->add_operation( "create",       "compound_file_create" );
        resc->add_operation( "open",         "compound_file_open" );
        resc->add_operation( "read",         "compound_file_read" );
        resc->add_operation( "write",        "compound_file_write" );
        resc->add_operation( "close",        "compound_file_close" );
        resc->add_operation( "unlink",       "compound_file_unlink" );
        resc->add_operation( "stat",         "compound_file_stat" );
        resc->add_operation( "fstat",        "compound_file_fstat" );
        resc->add_operation( "fsync",        "compound_file_fsync" );
        resc->add_operation( "mkdir",        "compound_file_mkdir" );
        resc->add_operation( "chmod",        "compound_file_chmod" );
        resc->add_operation( "opendir",      "compound_file_opendir" );
        resc->add_operation( "readdir",      "compound_file_readdir" );
        resc->add_operation( "stage",        "compound_file_stage" );
        resc->add_operation( "rename",       "compound_file_rename" );
        resc->add_operation( "freespace",    "compound_file_getfs_freespace" );
        resc->add_operation( "lseek",        "compound_file_lseek" );
        resc->add_operation( "rmdir",        "compound_file_rmdir" );
        resc->add_operation( "closedir",     "compound_file_closedir" );
        resc->add_operation( "truncate",     "compound_file_truncate" );
        resc->add_operation( "stagetocache", "compound_file_stage_to_cache" );
        resc->add_operation( "synctoarch",   "compound_file_sync_to_arch" );
        resc->add_operation( "registered",   "compound_file_registered" );
        resc->add_operation( "unregistered", "compound_file_unregistered" );
        resc->add_operation( "modified",     "compound_file_modified" );

        resc->add_operation( "redirect",     "compound_file_redirect" );

        // =-=-=-=-=-=-=-
        // set some properties necessary for backporting to iRODS legacy code
        resc->set_property< int >( "check_path_perm", 2 );//DO_CHK_PATH_PERM );
        resc->set_property< int >( "create_path",     1 );//CREATE_PATH );
        resc->set_property< int >( "category",        0 );//FILE_CAT );
        
        // =-=-=-=-=-=-=-
        // 4c. return the pointer through the generic interface of an
        //     eirods::resource pointer
        return dynamic_cast<eirods::resource*>( resc );
        
    } // plugin_factory

}; // extern "C" 



