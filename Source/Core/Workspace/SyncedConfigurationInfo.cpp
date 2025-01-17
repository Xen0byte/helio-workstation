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

#include "Common.h"
#include "SyncedConfigurationInfo.h"
#include "SerializationKeys.h"

#if !NO_NETWORK

SyncedConfigurationInfo::SyncedConfigurationInfo(const UserResourceDto &remote) :
    type(remote.getType()),
    name(remote.getName()),
    hash(remote.getHash()),
    updatedAt(remote.getUpdateTime()) {}

#endif

SerializedData SyncedConfigurationInfo::serialize() const
{
    using namespace Serialization::User;
    SerializedData root(Configurations::resource);

    root.setProperty(Configurations::type, this->type.toString());
    root.setProperty(Configurations::name, this->name);
    root.setProperty(Configurations::hash, this->hash);
    root.setProperty(Configurations::updatedAt, this->updatedAt.toMilliseconds());

    return root;
}

Identifier SyncedConfigurationInfo::getType() const noexcept
{
    return this->type;
}

String SyncedConfigurationInfo::getName() const noexcept
{
    return this->name;
}

void SyncedConfigurationInfo::deserialize(const SerializedData &data)
{
    this->reset();
    using namespace Serialization::User;

    const auto root = data.hasType(Configurations::resource) ?
        data : data.getChildWithName(Configurations::resource);

    if (!root.isValid()) { return; }

    this->type = root.getProperty(Configurations::type).toString();
    this->name = root.getProperty(Configurations::name);
    this->hash = root.getProperty(Configurations::hash);
    this->updatedAt = Time(root.getProperty(Configurations::updatedAt));
}

void SyncedConfigurationInfo::reset() {}

int SyncedConfigurationInfo::compareElements(const SyncedConfigurationInfo *first,
    const SyncedConfigurationInfo *second)
{
    jassert(first != nullptr && second != nullptr);
    if (first == second)
    {
        return 0;
    }

    return compareElements(first->type, first->name, second);
}

int SyncedConfigurationInfo::compareElements(const Identifier &type, const String &id,
    const SyncedConfigurationInfo *obj)
{
    const auto typeCompare = type.toString().compare(obj->type.toString());
    return typeCompare == 0 ? id.compare(obj->name) : typeCompare;
}

bool SyncedConfigurationInfo::equals(const ConfigurationResource::Ptr resource) const noexcept
{
    return this->type == resource->getResourceType() &&
        this->name == resource->getResourceId();
}
