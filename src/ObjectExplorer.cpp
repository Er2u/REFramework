#include <sstream>

#include <windows.h>

#include "REFramework.hpp"
#include "ObjectExplorer.hpp"

ObjectExplorer::ObjectExplorer()
{
    m_objectAddress.reserve(256);
}

void ObjectExplorer::onDrawUI() {
    ImGui::SetNextTreeNodeOpen(false, ImGuiCond_::ImGuiCond_Once);

    if (!ImGui::CollapsingHeader(getName().data())) {
        return;
    }

    auto curtime = std::chrono::system_clock::now();

    // List of globals to choose from
    if (ImGui::CollapsingHeader("Singletons")) {
        if (curtime > m_nextRefresh) {
            g_framework->getGlobals()->safeRefresh();
            m_nextRefresh = curtime + std::chrono::seconds(1);
        }

        // make a copy, we want to sort by name
        auto singletons = g_framework->getGlobals()->getObjects();

        // first loop, sort
        std::sort(singletons.begin(), singletons.end(), [](REManagedObject** a, REManagedObject** b) {
            auto aType = utility::REManagedObject::safeGetType(*a);
            auto bType = utility::REManagedObject::safeGetType(*b);

            if (aType == nullptr || aType->name == nullptr) {
                return true;
            }

            if (bType == nullptr || bType->name == nullptr) {
                return false;
            }

            return std::string_view{ aType->name } < std::string_view{ bType->name };
        });

        // Display the nodes
        for (auto obj : singletons) {
            auto t = utility::REManagedObject::safeGetType(*obj);

            if (t == nullptr || t->name == nullptr) {
                continue;
            }

            ImGui::SetNextTreeNodeOpen(false, ImGuiCond_::ImGuiCond_Once);

            if (ImGui::TreeNode(t->name)) {
                handleAddress(*obj);
                ImGui::TreePop();
            }

            contextMenu(*obj);
        }
    }

    ImGui::InputText("REObject Address", m_objectAddress.data(), 16, ImGuiInputTextFlags_::ImGuiInputTextFlags_CharsHexadecimal);

    if (m_objectAddress[0] != 0) {
        handleAddress(std::stoull(m_objectAddress, nullptr, 16));
    }
}

void ObjectExplorer::handleAddress(Address address, int32_t offset) {
    if (!isManagedObject(address)) {
        return;
    }

    auto object = address.as<REManagedObject*>();

    bool madeNode = false;
    auto isGameObject = utility::REManagedObject::isA(object, "via.GameObject");

    if (offset != -1) {
        ImGui::SetNextTreeNodeOpen(false, ImGuiCond_::ImGuiCond_Once);

        if (isGameObject) {
            madeNode = ImGui::TreeNode((uint8_t*)object + offset, "0x%X: %s", offset, utility::REString::getString(address.as<REGameObject*>()->name).c_str());
        }
        else {
            madeNode = ImGui::TreeNode((uint8_t*)object + offset, "0x%X: %s", offset, object->info->classInfo->type->name);
        }

        contextMenu(object);
    }

    if (madeNode || offset == -1) {
        if (isGameObject) {
            handleGameObject(address.as<REGameObject*>());
        }

        if (utility::REManagedObject::isA(object, "via.Component")) {
            handleComponent(address.as<REComponent*>());
        }

        handleType(utility::REManagedObject::getType(object));

        if (ImGui::TreeNode(object, "AutoGenerated Types")) {
            auto typeInfo = object->info->classInfo->type;

            for (auto i = (int32_t)sizeof(void*); i < typeInfo->size - sizeof(void*); i += sizeof(void*)) {
                auto ptr = Address(object).get(i).to<REManagedObject*>();

                handleAddress(ptr, i);
            }

            ImGui::TreePop();
        }
    }

    if (madeNode && offset != -1) {
        ImGui::TreePop();
    }
}

void ObjectExplorer::handleGameObject(REGameObject* gameObject) {
    ImGui::Text("Name: %s", utility::REString::getString(gameObject->name).c_str());
    makeTreeOffset(gameObject, offsetof(REGameObject, transform), "Transform");
    makeTreeOffset(gameObject, offsetof(REGameObject, folder), "Folder");
}

void ObjectExplorer::handleComponent(REComponent* component) {
    makeTreeOffset(component, offsetof(REComponent, ownerGameObject), "Owner");
    makeTreeOffset(component, offsetof(REComponent, childComponent), "ChildComponent");
    makeTreeOffset(component, offsetof(REComponent, prevComponent), "PrevComponent");
    makeTreeOffset(component, offsetof(REComponent, nextComponent), "NextComponent");
}

void ObjectExplorer::handleTransform(RETransform* transform) {

}

void ObjectExplorer::handleType(REType* t) {
    if (t == nullptr) {
        return;
    }

    auto count = 0;

    for (auto typeInfo = t; typeInfo != nullptr; typeInfo = typeInfo->super) {
        auto name = typeInfo->name;

        if (name == nullptr) {
            continue;
        }

        if (!ImGui::TreeNode(name)) {
            break;
        }

        ImGui::Text("Size: 0x%X", typeInfo->size);

        ++count;

        if (typeInfo->fields != nullptr && typeInfo->fields->variables != nullptr && typeInfo->fields->variables->data != nullptr) {
            auto descriptors = typeInfo->fields->variables->data->descriptors;

            if (ImGui::TreeNode("Fields", "Fields: %i", typeInfo->fields->variables->num)) {
                for (auto i = descriptors; i != descriptors + typeInfo->fields->variables->num; ++i) {
                    auto variable = *i;

                    if (variable == nullptr) {
                        continue;
                    }

                    ImGui::Text("%s %s", variable->typeName, variable->name);
                }

                ImGui::TreePop();
            }
        }
    }

    for (auto i = 0; i < count; ++i) {
        ImGui::TreePop();
    }
}

void ObjectExplorer::contextMenu(void* address) {
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::Selectable("Copy")) {
            std::stringstream ss;
            ss << std::hex << (uintptr_t)address;

            ImGui::SetClipboardText(ss.str().c_str());
        }
        
        // Log component hierarchy to disk
        if (isManagedObject(address) && utility::REManagedObject::isA((REManagedObject*)address, "via.Component") && ImGui::Selectable("Log Hierarchy")) {
            auto comp = (REComponent*)address;

            for (auto obj = comp; obj; obj = obj->childComponent) {
                auto t = utility::REManagedObject::safeGetType(obj);

                if (t != nullptr) {
                    if (obj->ownerGameObject == nullptr) {
                        spdlog::info("{:s} ({:x})", t->name, (uintptr_t)obj);
                    }
                    else {
                        auto owner = obj->ownerGameObject;
                        spdlog::info("[{:s}] {:s} ({:x})", utility::REString::getString(owner->name), t->name, (uintptr_t)obj);
                    }
                }

                if (obj->childComponent == comp) {
                    break;
                }
            }
        }

        ImGui::EndPopup();
    }
}

void ObjectExplorer::makeTreeOffset(REManagedObject* object, uint32_t offset, std::string_view name) {
    auto ptr = Address(object).get(offset).to<void*>();

    if (ptr == nullptr) {
        return;
    }

    auto madeNode = ImGui::TreeNode((uint8_t*)object + offset, "0x%X: %s", offset, name.data());

    contextMenu(ptr);

    if (madeNode) {
        handleAddress(ptr);
        ImGui::TreePop();
    }
}

bool ObjectExplorer::isManagedObject(Address address) const {
    return utility::REManagedObject::isManagedObject(address);
}
