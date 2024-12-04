#include <RegisterControllerV2.hpp>
#include <LicensePublic.hpp>
#include <AwpManager.hpp>
#include <RokuUtils.hpp>
#include <Registrations.hpp>
#include <ApiErrorUtils.hpp>
#include <CgiUtils.hpp>
#include <AzDBInfo.hpp>

#include <TokenHandler.hpp>
#include <DRMHelper.hpp>
#ifdef CONTAINER
#include <RCSDataParser.hpp>
#endif

using std::string;
using std::map;
using std::vector;
using namespace cassdb;
using namespace cgiutils;


namespace drm {

string handle_registration_request_v2 (map<string, string> &query_map,
                                       cgiutils::az_cgi_client_type_t
                                       client_type) {
    string action = ACTION_REGISTER;

//**CONTAINER checks wether the api is 'v1/reg' or not**//
#ifdef CONTAINER
    bool regInRollEnabled = false;
    //**get value for <regInRollEnabled> through RCSDataParser**//
    RCSDataParser::GetBoolRcsConfig(REGISTRATION_IN_ROLL_ENABLED, regInRollEnabled);
    AZ_LOG(VERBOSE, ACC, action + ": Value of RegistrationInRollEnabled retrieved from RCS is: " +
                      AZ_STR(regInRollEnabled ? "true" : "false"));
     
     //**if it is 'v1/reg' ? then skip reg, as v1/roll will perform registration**//
    if(regInRollEnabled && client_type == CGI_CLIENT_TYPE_V1) {
        string drm_skip_reg = "success";
        (void) AZ_LOG(DEBUG, DRM, action + ": Skipping Registration based on RCS Config");
        return (generate_drm_json(drm_skip_reg));
    }
#endif

   bool busy = false;

    if (client_type == CGI_CLIENT_TYPE_ROKU) {
        action = ACTION_ROKU_REGISTER;
    }

    // query_map contains owner_uid, either the one that was
    // provided in the query string, or the default one
    // confgiured for this box
    // verify that we do have an owner
    string owner_uid = query_map["owner_uid"];

    if (!cgiutils::cgiOwnerInit(owner_uid, &busy)) {
        if (busy) {
            return clientGenerateError(apierrorutils::GENERAL020,client_type);
        }
        // invalid owner
        return clientGenerateError(apierrorutils::DRM500,client_type);
    }

/*------------------------------------remaining code stays the same-----------------------------------*/