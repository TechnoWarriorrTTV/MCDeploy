#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

namespace MCDeploy {

// Membership + granular permissions per server.
// One record = (server_uuid, email) → role + permissions blob.
class MembersController : public drogon::HttpController<MembersController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(MembersController::listMembers,     "/api/servers/{uuid}/members",     Get);
        ADD_METHOD_TO(MembersController::addMember,       "/api/servers/{uuid}/members",     Post);
        ADD_METHOD_TO(MembersController::updateMember,    "/api/servers/{uuid}/members/{id}",Put);
        ADD_METHOD_TO(MembersController::removeMember,    "/api/servers/{uuid}/members/{id}",Delete);
        ADD_METHOD_TO(MembersController::listMyServers,   "/api/user/servers",               Get);
        ADD_METHOD_TO(MembersController::permissionCatalog,"/api/members/permission-catalog",Get);
    METHOD_LIST_END

    void listMembers(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void addMember(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid);
    void updateMember(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string id);
    void removeMember(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, std::string uuid, std::string id);
    void listMyServers(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void permissionCatalog(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);

private:
    bool validateJwt(const HttpRequestPtr& req, std::string& outUsername, std::string& outRole);
};

} // namespace MCDeploy
