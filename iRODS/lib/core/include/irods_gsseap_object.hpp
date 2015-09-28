#ifndef _gsseap_auth_object_H_
#define _gsseap_auth_object_H_

#include "irods_error.hpp"
#include "irods_auth_object.hpp"
#include "irods_stacktrace.hpp"

#include <gssapi/gssapi_eap.h>

namespace irods {

// constant key for gsseap auth object
const std::string GSSEAP_AUTH_PLUGIN( "GSSEAP" );
const std::string AUTH_GSSEAP_SCHEME( "gsseap" );

/**
 * @brief Auth object for GSSEAP authentication
 */
class gsseap_auth_object : public auth_object {
public:
    /// @brief Constructor
    gsseap_auth_object( rError_t* _r_error );
    virtual ~gsseap_auth_object();

    // Accessors

    /// @brief Returns the GSSEAP credentials
    virtual gss_cred_id_t creds( void ) const {
        return creds_;
    }

    /// @brief Returns the socket number
    virtual int sock( void ) const {
        return sock_;
    }

    /// @brief Returns the serverDN
    virtual const std::string& server_dn( void ) const {
        return server_dn_;
    }

    /// =-=-=-=-=-=-=-
    /// @brief serialize object to key-value pairs
    virtual error get_re_vars( keyValPair_t& );

    // Mutators

    /// @brief Sets the GSSEAP credentials
    virtual void creds( gss_cred_id_t _creds ) {
        creds_ = _creds;
    }

    /// @brief Sets the socket number
    virtual void sock( int s ) {
        sock_ = s;
    }

    /// @brief Sets the serverDN
    virtual void server_dn( const std::string& s ) {
        server_dn_ = s;
    }

    // Methods

    /// @brief undocumented
    error resolve( const std::string& _name, plugin_ptr& _plugin ); // resolve plugin

    /// @brief Comparison operator
    bool operator==( const gsseap_auth_object& _rhs ) const;


private:
    gss_cred_id_t creds_;
    int sock_;
    std::string server_dn_;
};

typedef boost::shared_ptr<gsseap_auth_object> gsseap_auth_object_ptr;

}; // namespace irods

#endif // _gsseap_auth_object_H_
