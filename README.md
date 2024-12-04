# Mediakind_RegInRoll
API optimisation for roll api to perform regestration in itself

****As to abide with company's policies, i have only uploaded the code developed by me, instead of uploading whole code base****

The Feature Request - 'RegInRoll' was to optimize the API performance by combining the ‘Registration request’ and ‘Roll request’ APIs into a single, enhanced /v1/roll API. This reduced delays in user authentication and subscription checks. Additionally, I updated the Remote Configuration System (XML-based) to remotely enable or disable this feature

**about APIs : **
>reg : 'https://ottappappgwampa.dev.mr.tv3cloud.com/client/reg?primary=XX&owner_uid=X&&user_token=XX'
the '/reg' API was the first API to be sent for authentication before starting playback. this API would carry user details (Primary is AccountId) as query parameters, which were used to insert corresponding details into 'Registrations' table and to retrive the data if already exists. then the /roll would be sent later.

>roll: 'https://ottappappgwampa.dev.mr.tv3cloud.com/client/roll?start_time=XX&primary=XX&owner_uid=XX&media_uid=LIVE%XX&user_token=XX&session_uid=XX&inhome=yes'
the '/roll' API was used to check the userId against the subscription, expiry date, offers availed and medias eligible for.

New Implementation - '---/v1/roll---' 
the newly implemented version of /roll is '/v1/roll' which was optimised to authenticate the user by checking the 'Registrations' table before performing the roll specific operations (corresponding changes are made in DrmRollController.cpp for 'v1/roll' api and RegisterController.cpp for 'v1/reg' API). this become one of the aids to reduce the playback delay. and to allow the backward compatibility, i made this feature to be remotly configurable (RCS) by adding the new-key value pair into XML based RCS - <regInRollEnabled>TorF</regInRollEnabled>.
the '/v1/roll' API would then check for RCS key through XMLParser (RCSDataParser()), then performed accordingly/
