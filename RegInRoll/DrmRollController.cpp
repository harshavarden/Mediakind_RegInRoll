/*
 * DrmRollController.cpp
 *
 * Created on: Jul 27, 2010
 * Copyright (c) Azuki Systems, Inc.
 *
 */

#include <DrmRollController.hpp>
#include <Roll.hpp>

#include "MediaMapping.hpp"
#include <DRMHelper.hpp>
#include <TokenHandler.hpp>
#include <RCSDataParser.hpp>
#endif

#include <vector>
#include <map>

using namespace azdb;
using namespace cgiutils;
using std::string;
using std::map;
using namespace cassdb;
using namespace redisdb;

namespace drm {

  //drm token rights elements 
static const vector<string> tokenvec = {"expires", "playcount","min-bitrate","max-bitrate","max-filesize","max-ads","min-playpos","max-playpos","cipher","key","key-length","analytics-url","wifi-blocked","hdmi-blocked","airplay-blocked","download-blocked","3G-blocked","4G-blocked","phone-tablet-desktop-stb-blocked-bits","analytics-enabled","max-rating","session-shift-enabled","activation","rw-enabled","ff-enabled","amc-debug-log-url","amc-debug-log-enabled","dma","recorded","jailbroken-blocked","max-jailbroken-bitrate","out-of-home-blocked","check-subscriber","in-home","pause-blocked","resume-blocked","seek-fwd-blocked","seek-rev-blocked","restart-window","restart-now-blocked","timeshift-bloked","ios-blocked","android-blocked","radius","casting-blocked","isContentPortabilityRoaming","NULL"};

void convert_drm_rights_to_string(string &rights);


/*------------------------------------------------other code exists------------------------------------------*/

/**We had 2 kinds of users - normal users and Guest  users
for normal users, registration is mandatory
for guest users, registration can be remotely enabled through**/

apierrorutils::az_api_err_t
authenticate_roll_request (map<string, string>& query_map,
                           map<string, string>& decrypted_map,
                           CryptedMessage *cm,
                           string& output,
                           bool rights_callout_enabled,
                           long long int bitmask,
                           bool &is_tve_guest_user,
                           CassTableMap &all_tables_for_roll,
                           cgiutils::az_cgi_client_type_t
                           client_type) {

    apierrorutils::az_api_err_t err_code;
    map<string, string>response_map;
    string owner_uid = query_map["owner_uid"];
    string owner_id = cgiutils::cgiOwnerGetId(owner_uid);
    string device_id = query_map["device_id"];
    string user_token = "";
    string primary = "";

    bool regInRollEnabled = false;

//** here, i am going to get RCS value for <RegistrationInRollEnabled> key**//
#ifdef CONTAINER
    RCSDataParser::GetBoolRcsConfig(REGISTRATION_IN_ROLL_ENABLED, regInRollEnabled);
    AZ_LOG(VERBOSE, DRM, "Value of RegistrationInRollEnabled retrieved from RCS is: " +
                      AZ_STR(regInRollEnabled ? "true" : "false"));
#endif
    //**FOR NORMAL USER CASE**//
    //**checking wether it is '/v1/roll'**//
    if (client_type == CGI_CLIENT_TYPE_V1) {
        //**Validating user details to be non-empty based on token recieved before checking 'Registrations' Table in DB**//
        decrypted_map["client_type"] = AZ_STR(CGI_CLIENT_TYPE_V1);
        if (query_map["primary"].empty()) {
            err_code = process_sts_token(query_map, response_map,
                                         is_tve_guest_user, client_type);
            if (err_code != apierrorutils::GENERAL000) {
                AZ_LOG(ERROR, ACC, "Authentication of the client making the roll "
                                "request failed");
                return err_code;
            }
            if (!response_map.empty()) {
                if(query_map["device_id"].empty() && query_map["prefer_sts_device_id"].empty()) {
                    device_id = response_map["DeviceId"];
                }
                user_token = (response_map["UserId"].empty()) ?  response_map["DefaultUserId"] : response_map["UserId"];
                primary = response_map["AccountId"];
                query_map["ci"] = user_token.empty() ?
                                primary : user_token;
                // All these values should be non-empty.
                // In case any of these is empty, registration should not
                // proced forward.
                // And return invalid sts token as error code.
                if (device_id.empty() || user_token.empty())
                {
                    AZ_LOG(ERROR, ACC, "Any of these values is empty in STS "
                           "token. Device id: " + device_id +
                           ", User token: " + user_token);
                    return (apierrorutils::DRM590);
                }
                query_map["user_token"] = user_token;
                query_map["dma"] = user_token;
                query_map["primary"] = primary;
                query_map["device_type"] = response_map["DeviceType"];
                
                //**after headers validation, let's start reg operation, Starting from here**//
                string account_id_plus_device_id = "";
                bool isFound = false;
                cassdb::cassdb_rc rc;
                //**Try to get Cassandra DB instance for Registrations table**//
                cassdb::Registrations *pReg;
                try {
                    pReg = cassdb::Registrations::getInstance(owner_id);
                } catch (...) {
                    AZ_LOG(ERROR, ACC, "Roll Handler: "
                           "Failed to get cassdb Registrations instance");
                    return (apierrorutils::GENERAL030);
                }

                if(!query_map["prefer_sts_device_id"].empty() && !query_map["device_id_sts"].empty())
                {
                    account_id_plus_device_id = query_map["primary"] +
                                                CONCAT_DELIM_ACC_DEV_IDS +
                                                query_map["device_id_sts"];
                    if (query_map["disable_guest_reg_operation"].empty()) {
                        //**get the table data for given user**//
                        rc = pReg->findRegistration(account_id_plus_device_id,
                                                    query_map["device_id_sts"]);
                        // database error?
                        if (rc != cassdb::SUCCESS) {
                            pReg->releaseRef();
                            return (apierrorutils::DRM504);
                        }

                        //**if for valid user, if no record found for coresponding device_id, then insert new data**//
                        if (pReg->size() == 0) {
                            if(regInRollEnabled) {
                                AZ_LOG(WARNING, DRM, "The accountId|deviceId combination: " +
                                        account_id_plus_device_id + " is not registered. "
                                        "Initiating Registration for this combination.");
                                        //**drm::insertNewRegistrationRecordForClientV1(accId,devId) inserts new record**//
                                if(GENERAL000 == drm::insertNewRegistrationRecordForClientV1(account_id_plus_device_id,
                                                                          query_map["device_id_sts"],
                                                                          query_map)) {
                                    AZ_LOG(DEBUG, DRM, "The accountId|deviceId combination: " +
                                            account_id_plus_device_id + " is Now registered. ");

                                    query_map["new_registration"] = "true";
                                    query_map["user_token"] = account_id_plus_device_id;
                                    query_map["device_id"] = query_map["device_id_sts"];
                                    query_map["session_uid"] = query_map["device_id_sts"] + CONCAT_DELIM_DEV_SESS_IDS + query_map["sessionId"];
                                    isFound = true;
                                } else {
                                    AZ_LOG(WARNING, DRM, "Failed to register the accountId|deviceId combination: " +
                                                         account_id_plus_device_id + " "
                                                         "Retry to find registration using the device id from device profile.");
                                }
                            } else {
                                AZ_LOG(WARNING, DRM, "The accountId|deviceId combination: " +
                                        account_id_plus_device_id + " is not registered. "
                                        "Authentication failed for device_id associated with sts token." +
                                        "Retry to find registration using the device id from device profile.");
                            }
                        } else {
                            query_map["device_id"] = query_map["device_id_sts"];
                            query_map["session_uid"] = query_map["device_id_sts"] + CONCAT_DELIM_DEV_SESS_IDS + query_map["sessionId"];
    #ifdef CONTAINER
                            cgiutils::setSessionInfoData("device_id", query_map["device_id"]);
    #endif
                            // Check whether this user/device is blocked
                            if (!(*pReg)[0]["enabled"].as<bool>()){
                                pReg->releaseRef();
                                return (apierrorutils::DRM555);
                            }
                            isFound = true;
                            all_tables_for_roll.insert(REGISTRATIONS_BY_USER_TOKEN_TBL, pReg);
                        }
                    } else {
                        query_map["device_id"] = query_map["device_id_sts"];
                        query_map["session_uid"] = query_map["device_id_sts"] + CONCAT_DELIM_DEV_SESS_IDS + query_map["sessionId"];
                    }
                }
                
                if(!isFound) {
                    account_id_plus_device_id = query_map["primary"] +
                                                CONCAT_DELIM_ACC_DEV_IDS +
                                                device_id;

                    //**Perform the same registration check for Guest user**/
                    if (query_map["disable_guest_reg_operation"].empty()) {
                        rc = pReg->findRegistration(account_id_plus_device_id,
                                                device_id);

                        // database error?
                        if (rc != cassdb::SUCCESS) {
                            pReg->releaseRef();
                            return (apierrorutils::DRM504);
                        }
                         
                        //**FOR GUEST USER CASE**// 
                        //**If no recoerd found for guest, then insert the record**/
                        if (pReg->size() == 0) {
                            if(regInRollEnabled) {
                                AZ_LOG(WARNING, DRM, "The accountId|deviceId combination: " +
                                        account_id_plus_device_id + " is not registered. "
                                        "Initiating Registration for this combination.");
                                if(GENERAL000 == drm::insertNewRegistrationRecordForClientV1(account_id_plus_device_id,
                                                                                             device_id,
                                                                                             query_map)) {
                                    AZ_LOG(DEBUG, DRM, "The accountId|deviceId combination: " +
                                            account_id_plus_device_id + " is Now registered. ");
                                    query_map["new_registration"] = "true";
                                    query_map["user_token"] = account_id_plus_device_id;
                                    query_map["device_id"] = device_id;
                                    isFound = true;
                                } else {
                                    AZ_LOG(ERROR, DRM, "Failed to register the accountId|deviceId combination: " + account_id_plus_device_id);
                                    pReg->releaseRef();
                                    return (apierrorutils::AMC384);
                                }
                            } else {
                                pReg->releaseRef();
                                AZ_LOG(ERROR, DRM, "The accountId|deviceId combination: " +
                                                account_id_plus_device_id + " is not registered. ");
                                return (apierrorutils::AMC383);
                            }
                        } else {
                            // Check whether this user/device is blocked
                            if (!(*pReg)[0]["enabled"].as<bool>()){
                                pReg->releaseRef();
                                return (apierrorutils::DRM555);
                            }
                            all_tables_for_roll.insert(REGISTRATIONS_BY_USER_TOKEN_TBL, pReg);
                        }
                    } else {
#ifdef CONTAINER
                        setSessionInfoData("device_id", query_map["device_id"]);
                        pReg->releaseRef();
                        return (apierrorutils::GENERAL000);
#endif
                    }
                }

                if (pReg->size() != 0) {
                    decrypted_map["account_group"] =
                         (*pReg)[0]["account_group"].as<string>("");
                    decrypted_map["cag_id"] =
                         (*pReg)[0]["client_account_group_id"].as<string>("");
                    decrypted_map["client_ip"] =
                         (*pReg)[0]["client_ip"].as<string>("");
                    decrypted_map["dma"] = (*pReg)[0]["dma"].as<string>("");
                    query_map["user_token"] = account_id_plus_device_id;
                    query_map["device_id"] = (*pReg)[0]["device_id"].as<string>("");
                } else {
                    if(regInRollEnabled && isFound) {
                        decrypted_map["account_group"] = query_map["primary"];
                        decrypted_map["cag_id"] = "";
                        decrypted_map["client_ip"] = query_map["client_ip"];
                        decrypted_map["dma"] = query_map["user_token"];
                    }
                    pReg->releaseRef();
                }
            }
        }
    } else {
        string action = ACTION_ROLL_AUTHENTICATE;
        vector<string> log_status;

        if (query_map["user_token"].empty()) {
            (void) AZ_LOG(INFO, DRM, action + ": " + DRM_LOG_ERROR_NO_USER);
            //ERROR_DRMSERVER_INVALID_USER
            return (apierrorutils::DRM501);
        }

        string port, error;

        // get 'DRM' HTTP Request header, decrypt it and validate
        user_token = query_map["user_token"];
        primary = query_map["primary"];

        err_code = process_roll_drm_token(owner_id,
                                          user_token,
                                          query_map["media_uid"],
                                          query_map["session_uid"],
                                          query_map[HTTP_DRM_HEADER],
                                          cm,
                                          decrypted_map,
                                          log_status);

        if (err_code == apierrorutils::GENERAL000) {
            // generic call
            if (rights_callout_enabled && (!isTV3RightsCalloutBlockedForRoll(bitmask))){
              AZ_LOG(DEBUG, ACC, "Rights callout enabled, hence skipping separate STS token verification");
            } else {
              err_code = process_sts_token(query_map, response_map, is_tve_guest_user);
            }
        }
        (void) print_log_status(action, log_status);
    }
    return (err_code);
}



/*------------------------------------------other code continues----------------------------------------*/
