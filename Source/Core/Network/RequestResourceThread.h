/*
    This file is part of Helio Workstation.

    Helio is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Helio is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Helio. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "HelioApiRoutes.h"
#include "HelioApiRequest.h"

class RequestResourceThread final : private Thread
{
public:
    
    RequestResourceThread() : Thread("RequestResource"), listener(nullptr) {}
    ~RequestResourceThread() override
    {
        this->stopThread(1000);
    }
    
    class Listener
    {
    public:
        virtual ~Listener() {}
    private:
        virtual void requestResourceOk(const ValueTree &resource) = 0;
        virtual void requestResourceFailed(const Array<String> &errors) = 0;
        virtual void requestResourceConnectionFailed() = 0;
        friend class RequestResourceThread;
    };
    
    void requestResource(RequestResourceThread::Listener *listener, String resourceName)
    {
        if (this->isThreadRunning())
        {
            return;
        }

        this->listener = listener;
        this->resourceName = resourceName;
        this->startThread(3);
    }
    
private:
    
    void run() override
    {
        const String uri = HelioFM::Api::V1::requestResource + "/" + this->resourceName.toLowerCase();
        const HelioApiRequest request(uri);
        this->response = request.get();

        if (this->response.result.failed())
        {
            MessageManager::getInstance()->callFunctionOnMessageThread([](void *ptr) -> void*
            {
                const auto self = static_cast<RequestResourceThread *>(ptr);
                self->listener->requestResourceConnectionFailed();
                return nullptr;
            }, this);
            return;
        }

        if (this->response.statusCode != 200)
        {
            MessageManager::getInstance()->callFunctionOnMessageThread([](void *ptr) -> void*
            {
                const auto self = static_cast<RequestResourceThread *>(ptr);
                self->listener->requestResourceFailed(self->response.errors);
                return nullptr;
            }, this);
            return;
        }

        MessageManager::getInstance()->callFunctionOnMessageThread([](void *ptr) -> void*
        {
            const auto self = static_cast<RequestResourceThread *>(ptr);
            // TODO get resource from the response
            self->listener->requestResourceOk(self->resource);
            return nullptr;
        }, this);
    }
    
    String resourceName;
    ValueTree resource;

    HelioApiRequest::Response response;
    RequestResourceThread::Listener *listener;
    
    friend class SessionManager;
};
