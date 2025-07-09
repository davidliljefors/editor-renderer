#include "Entity.h"

#include <algorithm>
#include <stdio.h>

#include "Editor.h"
#include "imgui.h"

static i32 s_nextId = 0;

static u64 s_object_id = 0;

u64 next_obj_id()
{
	return s_object_id ++ ;
}

Position get_position(ReadOnlySnapshot s, truth::Key objectId)
{
	const Entity* entity = (const Entity*)g_truth->read(s, objectId);

	Position res = entity->position;

	if (entity->prototype.asU64 != 0 && (entity->position.inheritsX || entity->position.inheritsY || entity->position.inheritsZ))
	{
		Position prototypePosition = get_position(s, entity->prototype);

		if (entity->position.inheritsX)
		{
			res.x = prototypePosition.x;

		}

		if (entity->position.inheritsY)
		{
			res.y = prototypePosition.y;
		}

		if (entity->position.inheritsZ)
		{
			res.z = prototypePosition.z;
		}
	}

	return res;
}

#define BREAK_ON_ERROR 1
#define DYNAMIC_DATA_ERROR(msg) printf("error: %s. line:%d\n", msg, __LINE__); if(BREAK_ON_ERROR)__debugbreak()

DynamicObjectDebugView DynamicData_DebugExpression(u64 hObject)
{
	DynamicObjectDebugView debugData;
	if (DynamicObject* pObject = lookup_obj(hObject))
	{
		for (u64 i = 0; i < pObject->flattened.names.size(); ++i)
		{
			DebugValuePair& pair = debugData.flattened.emplace_back();
			pair.name = string_repository_get(pObject->flattened.names[i]);
			pair.value = pObject->flattened.values[i];
		}

		for (u64 i = 0; i < pObject->owned.names.size(); ++i)
		{
			DebugValuePair& pair = debugData.owned.emplace_back();
			pair.name = string_repository_get(pObject->owned.names[i]);
			pair.value = pObject->owned.values[i];
		}

		for (u64 i = 0; i < pObject->instantiated.names.size(); ++i)
		{
			debugData.instantiated.emplace_back(string_repository_get(pObject->instantiated.names[i]));
		}
	}

	return debugData;
}

bool float_almost_equal(float a, float b) {
    const float epsilon = 0.0001f;
    return fabs(a - b) < epsilon;
}

void set_position(Transaction& tx, truth::Key objectId, Position p)
{
	Entity* entity = (Entity*)g_truth->edit(tx, objectId);
	Position current = get_position(tx.uncommitted.asImmutable(), objectId);

	if (entity->prototype.asU64 != 0)
	{
		if (entity->position.inheritsX && !float_almost_equal(current.x, p.x))
		{
			entity->position.inheritsX = false;
		}

		if (entity->position.inheritsY && !float_almost_equal(current.y,p.y))
		{
			entity->position.inheritsY = false;
		}

		if (entity->position.inheritsZ && !float_almost_equal(current.z, p.z))
		{
			entity->position.inheritsZ = false;
		}
	}

	entity->position.x = p.x;
	entity->position.y = p.y;
	entity->position.z = p.z;
}

void DynamicData_format_value(Printf& buf, DynamicData value)
{
	switch (value.type) {
	case DynamicData::Type_Null:
		buf.write("%s", "Null");
		break;
	case DynamicData::Type_Object:
		buf.write("Object[%d]", value.asObject()->flattened.values.size());
		break;
	case DynamicData::Type_Set:
		buf.write("Set[%d]", value.asSet()->flattened.values.size());
		break;
	case DynamicData::Type_Integer:
		buf.write("%lld", value.integer);
		break;
	case DynamicData::Type_Number:
		buf.write("%f", value.number);
		break;
	case DynamicData::Type_String:
		buf.write("%s", value.string);
		break;
	}
}

bool findName(const eastl::vector<u64>& vecNames, u64 hName, u64* outIndex)
{
	for (u64 i = 0; i < vecNames.size(); ++i)
	{
		if (hName == vecNames[i])
		{
			*outIndex = i;
			return true;
		}
	}
	return false;
}

bool findId(const eastl::vector<u64>& vecIds, u64 hName, u64* outIndex)
{
	for (u64 i = 0; i < vecIds.size(); ++i)
	{
		if (hName == vecIds[i])
		{
			*outIndex = i;
			return true;
		}
	}
	return false;
}

bool findValue(const eastl::vector<DynamicData>& vecValues, DynamicData* pValue, u64* outIndex)
{
	for (u64 i = 0; i < vecValues.size(); ++i)
	{
		if (vecValues[i].type == pValue->type && vecValues[i].hObject == pValue->hObject)
		{
			*outIndex = i;
			return true;
		}
	}
	return false;
}

void DynamicData_remove_from_set_(DynamicSet* pSet, DynamicData value)
{
	u64 i;
	if (findValue(pSet->added.values, &value, &i))
	{
		pSet->added.values.erase(pSet->added.values.begin() + i);
		pSet->flattened.dirty = true;
		pSet->version++;
	}
	else if (findValue(pSet->instantiated.values, &value, &i))
	{
		pSet->instantiated.ids.erase(pSet->instantiated.ids.begin() + i);
		pSet->instantiated.values.erase(pSet->instantiated.values.begin() + i);
		pSet->flattened.dirty = true;
		pSet->version++;
	}
	else if (findValue(pSet->flattened.values, &value, &i))
	{
		pSet->removed.values.push_back(value);
		pSet->flattened.dirty = true;
	}
}

void DynamicData_view_draw_object_id(DynamicObject* pObject)
{
	if (pObject->hPrototype == 0)
	{
		if (ImGui::TreeNodeEx(Printf("Object ID : %llu", pObject->id), ImGuiTreeNodeFlags_Leaf))
		{
			ImGui::TreePop();
		}
	}
	else
	{
		if (ImGui::TreeNodeEx(Printf("Object ID : %llu [Prototype ID : %llu]", pObject->id, pObject->hPrototype), ImGuiTreeNodeFlags_Leaf))
		{
			ImGui::TreePop();
		}
	}
}

void DynamicData_view_draw_value(DynamicData* nonContainer, DynamicData_MemberStatus status)
{
	switch (nonContainer->type)
	{
		case DynamicData::Type_Integer:
		case DynamicData::Type_Number:
		case DynamicData::Type_String:
		case DynamicData::Type_Null:
		{
			Printf buf;
			DynamicData_format_value(buf, *nonContainer);
			bool r = ImGui::TreeNodeEx(buf.cstr(), ImGuiTreeNodeFlags_Leaf);
			if (r)
			{
				ImGui::TreePop();
			}
			break;
		}
		default:
		{
			assert(false && "invalid type");
			break;
		}
	}
}

void DynamicData_view_object_context_menu(DynamicObject* pObject, u64 hMember, DynamicData_MemberStatus memberStatus, DynamicEditorPath* pPath)
{
	if (ImGui::IsItemClicked(1))
	{
		ImGui::OpenPopup("dd_view_context_menu");
	}

	if (ImGui::BeginPopup("dd_view_context_menu"))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
		ImGui::MenuItem(Printf("Member status : %s", to_string(memberStatus)));

		ImGui::BeginDisabled(memberStatus != MemberStatus_Inherited);
		if (ImGui::MenuItem("Instantiate object"))
		{
			//DynamicData_instantiate_path(pPath, member);
		}
		ImGui::EndDisabled();

		ImGui::BeginDisabled(memberStatus != MemberStatus_Instantiated);
		if (ImGui::MenuItem("Revert to prototype"))
		{
			//DynamicData_instantiate_clear(&value, hMemberName);
		}
		ImGui::EndDisabled();

		ImGui::PopStyleVar();
		ImGui::EndPopup();
	}
}

void DynamicData_view_draw_object(DynamicData* pValue, DynamicEditorPath* pPath)
{

	u64 objectType = DynamicData_obj_find(pValue, string_repository_hash(s_typeIdKey)).asUint();

	DynamicData* pTemplate = DynamicData_get_template(objectType);

	eastl::vector<u64> fields = pTemplate ? pTemplate

	DynamicObject* pObject = pValue->asObject();

	DynamicData_view_draw_object_id(pObject);

	for (u64 i = 0; i < size; ++i)
	{
		u64 hMember = pObject->flattened.names[i];
		DynamicData value = pObject->flattened.values[i];

		const char* memberName = string_repository_get(hMember);

		DynamicData_MemberStatus status = DynamicData_get_member_status(pPath, value);

		if (!value.isContainer())
		{
			DynamicData_view_draw_value(&value, status);
		}
		else
		{
			ImGui::PushID((int)value.id());
			bool childOpen = ImGui::TreeNode(memberName);

			DynamicData_view_object_context_menu(pObject, hMember, status, pPath);

			if (childOpen)
			{
				ImGui::TreePop();
			}


			ImGui::PopID();
		}
	}
}

void DynamicData_view_draw_object_set(DynamicData* pValue, u64 hSetName, DynamicEditorPath* pPath)
{
	eastl::vector<DynamicData> members = DynamicData_get_subobject_set(pValue, hSetName);
	eastl::vector<DynamicData> removed = DynamicData_get_subobject_set_locally_removed(pValue, hSetName);

	for (auto& r : removed)
		members.push_back(r);

	eastl::sort(members.begin(), members.end(), [](const DynamicData& a, const DynamicData& b) { return a.hObject < b.hObject; });


	for (u64 i = 0; i < members.size(); ++i)
	{
		DynamicData* item = &members[i];


	}
}

void DynamicData_view_impl(DynamicData* parent, u64 hMember, DynamicEditorPath* pPath)
{
	DynamicData value = hMember == 0 ? *parent : DynamicData_obj_find(parent, hMember);
	switch (value.type)
	{
	case DynamicData::Type_Object:
	{
		DynamicObject* pObject = value.asObject();
		DynamicData_obj_before_read(pObject);

		if (pObject->hPrototype == 0)
		{
			if (ImGui::TreeNodeEx(Printf("Object ID : %llu", value.hObject), ImGuiTreeNodeFlags_Leaf))
			{
				ImGui::TreePop();
			}
		}
		else
		{
			if (ImGui::TreeNodeEx(Printf("Object ID : %llu [Prototype ID : %llu]", value.hObject, pObject->hPrototype), ImGuiTreeNodeFlags_Leaf))
			{
				ImGui::TreePop();
			}
		}

		for (u64 i = 0; i < pObject->flattened.names.size(); ++i)
		{
			u64 hMemberName = pObject->flattened.names[i];
			DynamicData member = pObject->flattened.values[i];

			DynamicData_MemberStatus memberStatus = MemberStatus_None;
			bool isContainer = pObject->flattened.values[i].isContainer();
			if (isContainer)
			{
				memberStatus = DynamicData_get_member_status(pPath, member);
				pPath->values.push_back(pObject->flattened.values[i]);
			}

			const char* name = string_repository_get(hMemberName);

			if (!isContainer)
			{
				Printf buf;
				DynamicData element = pObject->flattened.values[i];
				DynamicData_format_value(buf, pObject->flattened.values[i]);
				if (ImGui::TreeNodeEx(Printf("%s : %s", name, buf.cstr()), ImGuiTreeNodeFlags_Leaf))
				{
					ImGui::TreePop();
				}

				if (ImGui::IsItemClicked(0) && ImGui::IsKeyDown(ImGuiKey_LeftShift))
				{
					__debugbreak();

					memberStatus = DynamicData_get_member_status(pPath, member);
				}

				if (element.type == DynamicData::Type_Number && ImGui::IsItemClicked())
				{
					DynamicData newPos = DynamicData_make_num(element.asNumber() + 1);
					DynamicData_obj_set(&value, hMemberName, newPos);
				}
			}
			else
			{

				if (memberStatus == MemberStatus_Inherited)
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
				bool childOpen = ImGui::TreeNode(name);

				if (ImGui::IsItemClicked(0) && ImGui::IsKeyDown(ImGuiKey_LeftShift))
				{
					__debugbreak();
					pPath->values.pop_back();
					memberStatus = DynamicData_get_member_status(pPath, member);
					pPath->values.push_back(pObject->flattened.values[i]);
				}

				ImGui::PushID((int)i);
				if (ImGui::IsItemClicked(1))
				{
					ImGui::OpenPopup("dd_view_context_menu");
				}

				if (ImGui::BeginPopup("dd_view_context_menu"))
				{
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
					ImGui::MenuItem(Printf("Member status : %s", to_string(memberStatus)));

					ImGui::BeginDisabled(memberStatus != MemberStatus_Inherited);
					if (ImGui::MenuItem("Instantiate object"))
					{
						DynamicData_instantiate_path(pPath, member);
					}
					ImGui::EndDisabled();

					ImGui::BeginDisabled(memberStatus != MemberStatus_Instantiated);
					if (ImGui::MenuItem("Revert to prototype"))
					{
						DynamicData_instantiate_clear(&value, hMemberName);
					}
					ImGui::EndDisabled();

					ImGui::PopStyleVar();
					ImGui::EndPopup();
				}

				ImGui::PopID();

				if (childOpen)
				{
					if (ImGui::IsItemClicked(0) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
					{
						pObject->flattened.basedOnVersion = 0;
						DynamicData_obj_before_read(pObject);

						DynamicData next = pObject->flattened.values[i];
						if (DynamicObject* pNext = next.asObject())
						{
							pNext->flattened.basedOnVersion = 0;
							DynamicData_obj_before_read(pNext);
						}
					}



					DynamicData_view_impl(&value, hMemberName, pPath);

					ImGui::TreePop();
				}

				if (memberStatus == MemberStatus_Inherited)
					ImGui::PopStyleVar();

				pPath->values.pop_back();
			}
		}
		break;
	}
	case DynamicData::Type_Set:
	{
		eastl::vector<DynamicData> set; // include removed items for display

		DynamicSet* pSet = value.asSet();
		DynamicData_set_before_read(parent, hMember);
		
		for (DynamicData member : pSet->flattened.values)
		{
			set.push_back(member);
		}
		for (DynamicData removed : pSet->removed.values)
		{
			set.push_back(removed);

		}

		eastl::sort(set.begin(), set.end(), [](const DynamicData& a, const DynamicData& b) { return a.hObject < b.hObject; });

		for (DynamicData member : set)
		{
			DynamicData_MemberStatus memberStatus = DynamicData_get_member_status(pPath, member);

			pPath->values.push_back(member);

			if (memberStatus == MemberStatus_Inherited)
			{
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5);
			}
			if (memberStatus == MemberStatus_Removed)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
			}
				

			ImGui::PushID((int)member.hObject);
			bool open = ImGui::TreeNodeEx("Set Element");
			if (ImGui::IsItemClicked(1))
			{
				ImGui::OpenPopup("dd_view_context_menu");
			}

			if (memberStatus == MemberStatus_Removed)
			{
				ImGui::PopStyleColor();
			}

			if (ImGui::BeginPopup("dd_view_context_menu"))
			{
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
				ImGui::MenuItem(Printf("Member status : %s", to_string(memberStatus)));

				ImGui::BeginDisabled(memberStatus != MemberStatus_Inherited);
				if (ImGui::MenuItem("Instantiate object"))
				{
					//DynamicData_instantiate_path(pPath, memberId);
				}
				ImGui::EndDisabled();

				ImGui::BeginDisabled(memberStatus != MemberStatus_Instantiated);
				if (ImGui::MenuItem("Revert to prototype"))
				{
					//DynamicData_instantiate_clear(pValue, );
				}
				ImGui::EndDisabled();
				
				if (memberStatus == MemberStatus_Added || memberStatus == MemberStatus_Owned || memberStatus == MemberStatus_Inherited)
				{
					if (ImGui::MenuItem("Delete"))
					{
						DynamicData_remove_from_set_(pSet, member);
					}
				}
				if (memberStatus == MemberStatus_Instantiated)
				{
					if (ImGui::MenuItem("Revert"))
					{
						//DynamicData_remove_from_set_(pSet, memberId);
					}
				}
				if (memberStatus == MemberStatus_Removed)
				{
					if (ImGui::MenuItem("Cancel remove"))
					{

					}
				}

				ImGui::PopStyleVar();
				ImGui::EndPopup();
			}
			ImGui::PopID();

			if (open)
			{
				DynamicData_view_impl(parent, hMember, pPath);
				ImGui::TreePop();
			}


			if (memberStatus == MemberStatus_Inherited)
			{
				ImGui::PopStyleVar();
			}

			pPath->values.pop_back();
		}

		//
		//}u64 size = DynamicData_size(pValue);
		//for (u64 i = 0; i < size; ++i)
		//{
		//	DynamicData member = pSet->flattened.values[i];

		//	DynamicData_MemberStatus memberStatus = MemberStatus_None;

		//	bool isContainer = pSet->flattened.values[i].isContainer();
		//	if (isContainer)
		//	{
		//		memberStatus = DynamicData_get_member_status(pPath, member);
		//		pPath->values.push_back(pSet->flattened.values[i]);
		//	}

		//	if (memberStatus == MemberStatus_Inherited)
		//	{
		//		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5);
		//	}

		//	bool open = ImGui::TreeNode(Printf("Element %d", i));
		//	ImGui::PushID((int)member.hObject);
		//	if (ImGui::IsItemClicked(1) && isContainer)
		//	{
		//		ImGui::OpenPopup("dd_view_context_menu");
		//	}

		//	if (ImGui::BeginPopup("dd_view_context_menu"))
		//	{
		//		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
		//		ImGui::MenuItem(Printf("Member status : %s", to_string(memberStatus)));

		//		ImGui::BeginDisabled(memberStatus != MemberStatus_Inherited);
		//		if (ImGui::MenuItem("Instantiate object"))
		//		{
		//			//DynamicData_instantiate_path(pPath, memberId);
		//		}
		//		ImGui::EndDisabled();

		//		ImGui::BeginDisabled(memberStatus != MemberStatus_Instantiated);
		//		if (ImGui::MenuItem("Revert to prototype"))
		//		{
		//			//DynamicData_instantiate_clear(pValue, );
		//		}
		//		ImGui::EndDisabled();
		//		
		//		if (memberStatus == MemberStatus_Added || memberStatus == MemberStatus_Owned || memberStatus == MemberStatus_Inherited)
		//		{
		//			if (ImGui::MenuItem("Delete"))
		//			{
		//				DynamicData_remove_from_set_(pSet, member);
		//			}
		//		}
		//		if (memberStatus == MemberStatus_Instantiated)
		//		{
		//			if (ImGui::MenuItem("Revert"))
		//			{
		//				//DynamicData_remove_from_set_(pSet, memberId);
		//			}
		//		}

		//		ImGui::PopStyleVar();
		//		ImGui::EndPopup();
		//	}
		//	ImGui::PopID();

		//	if (open)
		//	{
		//		DynamicData_view_impl(&pSet->flattened.values[i], memberStatus, pPath);
		//		ImGui::TreePop();
		//	}

		//	if (memberStatus == MemberStatus_Inherited)
		//	{
		//		ImGui::PopStyleVar();
		//	}

		//	if (isContainer)
		//	{
		//		pPath->values.pop_back();
		//	}
		break;
	}
	case DynamicData::Type_Integer:
	case DynamicData::Type_Number:
	case DynamicData::Type_String:
	case DynamicData::Type_Null:
	{
		Printf buf;
		DynamicData_format_value(buf, value);
		bool r = ImGui::TreeNodeEx(buf.cstr(), ImGuiTreeNodeFlags_Leaf);
		if (r)
		{
			ImGui::TreePop();
		}
		break;
	}
	}
}

static eastl::unordered_map<u64, const char*> s_string_repository;
static std::unordered_map<u64, DynamicData> g_templates;
static std::unordered_map<u64, DynamicDataParser_i> g_parsers;
static std::unordered_map<u64, DynamicObject*> g_objects;
static std::unordered_map<u64, DynamicSet*> g_sets;

DynamicDataParser_i* lookup_parser(u64 typeId)
{
	auto find = g_parsers.find(typeId);
	return find != g_parsers.end() ? &find->second : nullptr;
}

DynamicObject* lookup_obj(u64 hObject)
{
	auto find = g_objects.find(hObject);
	return find != g_objects.end() ? find->second : nullptr;
}

DynamicSet* lookup_set(u64 hSet)
{
	auto find = g_sets.find(hSet);
	return find != g_sets.end() ? find->second : nullptr;
}

bool DDObject_is_up_to_date(DynamicObject* pObject, DynamicObject* pPrototype)
{
	if (pObject->flattened.basedOnVersion == pPrototype->version && !pObject->flattened.dirty && !pPrototype->flattened.dirty)
	{
		if (pPrototype->hPrototype != 0)
		{
			DynamicObject* pNextPrototype = lookup_obj(pPrototype->hPrototype);
			return DDObject_is_up_to_date(pPrototype, pNextPrototype);
		}
		else
		{
			return true;
		}
	}
	return false;
}

DynamicData DynamicData_obj_new()
{
	DynamicData value;

	DynamicObject* pObject = new DynamicObject();

	u64 id = next_obj_id();

	value.type = DynamicData::Type_Object;
	value.hObject = id;

	pObject->version = 1;
	pObject->id = id;

	g_objects[id] = pObject;

	return value;
}

DynamicData DynamicData_instance_new(DynamicData* prototype)
 {
	DynamicData value = DynamicData_make_null();

	if (prototype->type == DynamicData::Type_Object)
	{
		DynamicObject* pObject = new DynamicObject();

		u64 id = next_obj_id();
		value.type = DynamicData::Type_Object;
		value.hObject = id;
		pObject->hPrototype = prototype->id();
		pObject->flattened.basedOnVersion = 0;
		pObject->version = 1;

		DynamicData_obj_before_read(pObject);

		g_objects[id] = pObject;
	}

	return value;
}

DynamicData DynamicData_new_from_prototype(DynamicData* pPrototype)
{
	DynamicData instance = DynamicData_instance_new(pPrototype);
	return instance;
}

DynamicData DynamicData_instantiate_member_impl(DynamicObject* pObject, u64 hName)
{
	DynamicData_obj_before_read(pObject);
	u64 i;
	// make sure its not already instantiated
	if (!findName(pObject->instantiated.names, hName, &i))
	{
		// find our value from the prototype
		if (findName(pObject->flattened.names, hName, &i))
		{
			DynamicData* pPrototype = &pObject->flattened.values[i];
			DynamicData instance = DynamicData_new_from_prototype(pPrototype);
			pObject->instantiated.names.push_back(hName);
			pObject->instantiated.values.push_back(instance);

			// todo this has to be set so we dont have to reset
			pObject->flattened.dirty = true;


			DynamicData_obj_before_read(pObject);
			return instance;
		}
	}

	return DynamicData_make_null();
}

DynamicData DynamicData_instantiate_member(DynamicData* pValue, u64 hName)
{
	DynamicObject* pObject = pValue->asObject();
	return DynamicData_instantiate_member_impl(pObject, hName);
}

void DynamicData_instantiate_clear(DynamicData* pValue, u64 hName)
{
	if (pValue->type == DynamicData::Type_Object)
	{
		DynamicObject* pObject = pValue->asObject();
		u64 i;
		if (findName(pObject->instantiated.names, hName, &i))
		{
			DynamicData instance = pObject->instantiated.values[i];

			if (instance.type == DynamicData::Type_Object)
			{
				DynamicObject* pInstance = instance.asObject();
				pInstance->tombstone = true;
				pObject->instantiated.values.erase(pObject->instantiated.values.begin() + i);
				pObject->instantiated.names.erase(pObject->instantiated.names.begin() + i);
				pObject->flattened.dirty = true;	
			}
		}
	}
	if (pValue->type == DynamicData::Type_Set)
	{
		DynamicSet* pSet = pValue->asSet();
		u64 i;
		if (findName(pSet->instantiated.ids, hName, &i))
		{
			DynamicData instance = pSet->instantiated.values[i];

			if (instance.type == DynamicData::Type_Object)
			{
				DynamicObject* pInstance = instance.asObject();
				pInstance->tombstone = true;
				pSet->instantiated.values.erase(pSet->instantiated.values.begin() + i);
				pSet->instantiated.ids.erase(pSet->instantiated.ids.begin() + i);
				pSet->flattened.dirty = true;
			}
		}
	}
}

DynamicData DynamicData_set_new()
{
	DynamicData value;

	u64 id = next_obj_id();

	DynamicSet* pSet = new DynamicSet();
	value.type = DynamicData::Type_Set;
	value.pSet = new DynamicSet();
	pSet->version = 1;

	return value;

}

DynamicData DynamicData_str_new()
{
	DynamicData value;

	value.type = DynamicData::Type_String;
	value.string = nullptr;

	return value;
}

DynamicData DynamicData_int_new()
{
	DynamicData value;

	value.type = DynamicData::Type_Integer;
	value.integer = 0;

	return value;
}

DynamicData DynamicData_num_new()
{
	DynamicData value;

	value.type = DynamicData::Type_Number;
	value.number = 0.0;

	return value;
}

DynamicData DynamicData_make_int(i64 integer)
{
	DynamicData value = DynamicData_int_new();
	value.integer = integer;
	return value;
}

DynamicData DynamicData_make_str(const char* str)
{
	DynamicData value = DynamicData_str_new();

	u64 capacity = strlen(str) + 1;
	value.string = (char*)malloc(capacity);
	strcpy_s(value.string, capacity, str);

	return value;
}

DynamicData DynamicData_make_null()
{
	DynamicData data;
	data.type = DynamicData::Type_Null;
	data.integer = 0;
	return data;
}

DynamicData DynamicData_make_num(f64 number)
{
	DynamicData value = DynamicData_num_new();
	value.number = number;
	return value;
}

DynamicData DyancmiData_instantiate_subobject_from_set(DynamicData* pValue, u64 hSetMember, DynamicData item)
{
	if (DynamicObject* pObject = pValue->asObject())
	{
		if (pObject->hPrototype == 0)
		{
			DYNAMIC_DATA_ERROR("Instantiate only valid on objects with a prototype");
		}

		u64 i;
		if (findName(pObject->instantiated.names, hSetMember, &i))
		{
			if (DynamicSet* pSet = pObject->instantiated.values[i].asSet())
			{
				if (findValue(pSet->flattened.values, &item, &i))
				{
					DynamicData prototype = pSet->flattened.values[i];
					DynamicData instantiated = DynamicData_new_from_prototype(&prototype);
					pSet->instantiated.ids.push_back(item.id());
					pSet->instantiated.values.push_back(instantiated);
					pSet->flattened.dirty = true;
					return instantiated;
				}
			}
		}
		else if (findName(pObject->owned.names, hSetMember, &i))
		{
			if (DynamicSet* pSet = pObject->owned.values[i].asSet())
			{
				if (findValue(pSet->flattened.values, &item, &i))
				{
					DynamicData prototype = pSet->flattened.values[i];
					DynamicData instantiated = DynamicData_new_from_prototype(&prototype);
					pSet->instantiated.ids.push_back(item.id());
					pSet->instantiated.values.push_back(instantiated);
					pSet->flattened.dirty = true;
					return instantiated;
				}
			}
		}
		else
		{
			DYNAMIC_DATA_ERROR(Printf("Object does not contain member [%s]", string_repository_get(hSetMember)).cstr());
		}
	} 

	return DynamicData_make_null();
}

void DynamicData_add_to_subobject_set(DynamicData* pValue, u64 hSetMember, DynamicData item)
{
	if (DynamicObject* pObject = pValue->asObject())
	{
		u64 i;
		if (findName(pObject->owned.names, hSetMember, &i))
		{
			if (DynamicSet* pSet = pObject->owned.values[i].asSet())
			{
				pSet->added.values.push_back(item);
				pSet->flattened.dirty = true;
				pSet->version++;
			}
		}
		else if (findName(pObject->instantiated.names, hSetMember, &i))
		{
			if (DynamicSet* pSet = pObject->instantiated.values[i].asSet())
			{
				pSet->added.values.push_back(item);
				pSet->flattened.dirty = true;
				pSet->version++;
			}
		}
		else if (findName(pObject->overrides.names, hSetMember, &i))
		{
			if (DynamicSet* pSet = pObject->overrides.values[i].asSet())
			{
				pSet->added.values.push_back(item);
				pSet->flattened.dirty = true;
				pSet->version++;
			}
		}
		else
		{
			DYNAMIC_DATA_ERROR(Printf("Object does not contain member [%s]", string_repository_get(hSetMember)).cstr());
		}
	}
	else
	{
		DYNAMIC_DATA_ERROR("pValue is not of type Object");
	}
}

void DynamicData_remove_from_subobject_set(DynamicData* pValue, u64 hMember, DynamicData item)
{
	if (DynamicObject* pObject = pValue->asObject())
	{
		DynamicData_obj_before_read(pObject);

		if (pObject->hPrototype == 0)
		{
			u64 i;
			if (findName(pObject->owned.names, hMember, &i))
			{
				if (DynamicSet* pSet = pObject->owned.values[i].asSet())
				{
					if (findValue(pSet->added.values, &item, &i))
					{
						pSet->added.values.erase(pSet->added.values.begin() + i);
						pSet->flattened.dirty = true;
						pSet->version++;
						pObject->version++;

						return;
					}
					DYNAMIC_DATA_ERROR("object not in set");
				}
				DYNAMIC_DATA_ERROR("no set in object[hMember]");
			}
			DYNAMIC_DATA_ERROR("didnt find key");
		}
	}
}

void DynamicData_remove_from_prototype_subobject_set(DynamicData* pValue, u64 hName, u64 id)
{
}

void DynamicData_cancel_remove_from_prototype_subobject_set(DynamicData* pValue, u64 hName, u64 id)
{
}

eastl::vector<DynamicData> DynamicData_get_subobject_set(DynamicData* pValue, u64 hSetName)
{
	eastl::vector<DynamicData> setMembers;

	if (DynamicObject* pObject = pValue->asObject())
	{
		u64 i;
		if (findName(pObject->flattened.names, hSetName, &i))
		{
			if (DynamicSet* pSet = pObject->flattened.values[i].asSet())
			{
				setMembers = pSet->flattened.values;
			}
		}
	}

	return setMembers;
}

eastl::vector<DynamicData> DynamicData_get_subobject_set_locally_removed(DynamicData* pValue, u64 hSetName)
{
	eastl::vector<DynamicData> setMembers;

	if (DynamicObject* pObject = pValue->asObject())
	{
		u64 i;
		if (findName(pObject->flattened.names, hSetName, &i))
		{
			if (DynamicSet* pSet = pObject->flattened.values[i].asSet())
			{
				setMembers = pSet->removed.values;
			}
		}
	}

	return setMembers;
}

bool DynamicData_obj_is_editable(DynamicData* pValue, u64 hName)
{
	if (DynamicObject* pObject = pValue->asObject())
	{
		if (pObject->hPrototype)
		{
			u64 i;
			if (findName(pObject->instantiated.names, hName, &i))
			{
				return true;
			}

			// todo can we add names in a variant?
		}
		else
		{
			u64 i;
			if (findName(pObject->owned.names, hName, &i))
			{
				return true;
			}
		}
	}

	return false;
}

void DynamicData_instantiate_path_impl(DynamicEditorPath* pPath, DynamicObject* pCurrent, u64 depth, DynamicData targetValue);

void DynamicData_instantiate_path_impl(DynamicEditorPath* pPath, DynamicSet* pCurrent, u64 depth, DynamicData targetValue)
{
	//if (depth > pPath->values.size())
	//{
	//	return; // were done
	//}

	//bool isAtEnd = pPath->values.size() == depth;
	//DynamicData currentValue = isAtEnd ? targetValue : pPath->values[depth];

	//if (pCurrent->hPrototype)
	//{
	//	u64 i;
	//	if (findValue(pCurrent->instantiated.values, &currentValue, &i))
	//	{
	//		// already instantiated at this depth
	//		DynamicObject* pNext = pCurrent->instantiated.values[i].asObject();
	//		DynamicData_instantiate_path_impl(pPath, pNext, depth + 1, currentValue);
	//	}
	//	else if (findValue(pCurrent->flattened.values, &currentValue, &i))
	//	{
	//		// not instantiated, but exists in the flattened state. Instantiate it.
	//		DynamicData next = DynamicData_instantiate_from_set_impl(pCurrent, targetValue);
	//		if (next.type == DynamicData::Type_Object)
	//		{
	//			DynamicObject* pNext = next.asObject();
	//			DynamicData_obj_before_read(pNext);
	//			DynamicData_instantiate_path_impl(pPath, pNext, depth + 1, targetValue);
	//		}
	//		else
	//		{
	//			DynamicSet* pNext = next.asSet();
	//			DynamicData_set_before_read(pNext);
	//			DynamicData_instantiate_path_impl(pPath, pNext, depth + 1, targetValue);
	//		}
	//	}
	//}
	//else
	//{
	//	u64 i;
	//	if (findValue(pCurrent->added.values, &currentValue, &i))
	//	{
	//		// Value is added, nothing to do at this level
	//		if (pCurrent->added.values[i].type == DynamicData::Type_Object)
	//		{
	//			DynamicObject* pNext = pCurrent->added.values[i].asObject();
	//			DynamicData_instantiate_path_impl(pPath, pNext, depth + 1, targetValue);
	//		}
	//		else if (pCurrent->added.values[i].type == DynamicData::Type_Set)
	//		{
	//			DynamicSet* pNext = pCurrent->added.values[i].asSet();
	//			DynamicData_instantiate_path_impl(pPath, pNext, depth + 1, targetValue);
	//		}
	//	}
	//}
}

void DynamicData_instantiate_path_impl(DynamicEditorPath* pPath, DynamicObject* pCurrent, u64 depth, DynamicData targetValue)
{
	if (depth > pPath->values.size())
	{
		return; // were done
	}

	bool isAtEnd = pPath->values.size() == depth;
	DynamicData currentValue = isAtEnd ? targetValue : pPath->values[depth];

	if (pCurrent->hPrototype)
	{
		u64 i;
		if (findValue(pCurrent->instantiated.values, &currentValue, &i))
		{
			// already instantiated at this depth
			DynamicData next = pCurrent->instantiated.values[i];
			if (next.type == DynamicData::Type_Object)
			{
				DynamicData_instantiate_path_impl(pPath, next.asObject(), depth + 1, targetValue);
			}
			if (next.type == DynamicData::Type_Set)
			{
				DynamicData_instantiate_path_impl(pPath, next.asSet(), depth + 1, targetValue);
			}
		}
		else if (findValue(pCurrent->flattened.values, &currentValue, &i))
		{
			// not instantiated, but exists in the flattened state. Instantiate it.
			u64 hName = pCurrent->flattened.names[i];
			DynamicData next = DynamicData_instantiate_member_impl(pCurrent, hName);
			if (next.type == DynamicData::Type_Object)
			{
				DynamicObject* pNext = next.asObject();
				DynamicData_obj_before_read(pNext);
				DynamicData_instantiate_path_impl(pPath, pNext, depth + 1, targetValue);
			}
			else
			{
				DynamicSet* pNext = next.asSet();
				DynamicData_set_before_read(pCurrent, hName);
				DynamicData_instantiate_path_impl(pPath, pNext, depth + 1, targetValue);
			}
		}
	}
	else
	{
		u64 i;
		if (findValue(pCurrent->owned.values, &currentValue, &i))
		{
			// Value is owned, nothing to do at this level
			if (pCurrent->owned.values[i].type == DynamicData::Type_Object)
			{
				DynamicObject* pNext = pCurrent->owned.values[i].asObject();
				DynamicData_instantiate_path_impl(pPath, pNext, depth + 1, targetValue);
			}
			else if (pCurrent->owned.values[i].type == DynamicData::Type_Set)
			{
				DynamicSet* pNext = pCurrent->owned.values[i].asSet();
				DynamicData_instantiate_path_impl(pPath, pNext, depth + 1, targetValue);
			}
		}
	}
}

void DynamicData_instantiate_path(DynamicEditorPath* pPath, DynamicData value)
{
	DynamicObject* pCurrent = pPath->root;
	DynamicData_instantiate_path_impl(pPath, pCurrent, 0, value);
}

const char* to_string(DynamicData_MemberStatus status)
{
	switch (status) {
	case MemberStatus_Owned:
		return "Owned";
	case MemberStatus_Added:
		return "Added";
	case MemberStatus_Removed:
		return "Removed";
	case MemberStatus_Inherited:
		return "Inherited";
	case MemberStatus_Instantiated:
		return "Instantiated";
	case MemberStatus_None:
		return "None";
	}

	assert(false && "enum out of range");

	return "enum : error";
}

DynamicData_MemberStatus DynamicData_obj_get_member_status_impl(DynamicEditorPath* pPath, DynamicObject* pObject, u64 depth, DynamicData targetValue);

DynamicData_MemberStatus DynamicData_set_get_member_status_impl(DynamicEditorPath* pPath, DynamicObject* pObject, u64 hSetName, u64 depth, DynamicData targetValue)
{
	//bool isAtEnd = pPath->values.size() == depth;
	//DynamicData currentValue = isAtEnd ? targetValue : pPath->values[depth];


	//if (pObject->hPrototype)
	//{
	//	u64 i;
	//	if (findValue(pCurrent->instantiated.values, &currentValue, &i))
	//	{
	//		if (isAtEnd)
	//		{
	//			return MemberStatus_Instantiated;
	//		}
	//		// already instantiated at this depth

	//		if (pCurrent->instantiated.values[i].type == DynamicData::Type_Object)
	//		{
	//			DynamicObject* pNext = pCurrent->instantiated.values[i].asObject();
	//			return DynamicData_obj_get_member_status_impl(pPath, pNext, depth + 1, targetValue);
	//		}
	//		else
	//		{
	//			DynamicSet* pNext = pCurrent->instantiated.values[i].asSet();
	//			return DynamicData_set_get_member_status_impl(pPath, pNext, depth + 1, targetValue);
	//		}
	//	}
	//	else if (findValue(pCurrent->added.values, &currentValue, &i))
	//	{
	//		return MemberStatus_Added;
	//	}
	//	else if (findValue(pCurrent->removed.values, &currentValue, &i))
	//	{
	//		return MemberStatus_Removed;
	//	}
	//	else if (findValue(pCurrent->flattened.values, &currentValue, &i))
	//	{
	//		// if we enter any inhertied value we can early out.
	//		return MemberStatus_Inherited;
	//	}
	//}
	//else
	//{
	//	u64 i;
	//	if (findValue(pCurrent->added.values, &currentValue, &i))
	//	{
	//		if (isAtEnd)
	//		{
	//			return MemberStatus_Added;
	//		}

	//		// Value is added, nothing to do at this level. can continue
	//		if (pCurrent->added.values[i].type == DynamicData::Type_Object)
	//		{
	//			DynamicObject* pNext = pCurrent->added.values[i].asObject();
	//			return DynamicData_obj_get_member_status_impl(pPath, pNext, depth + 1, targetValue);
	//		}
	//		else
	//		{
	//			DynamicSet* pNext = pCurrent->added.values[i].asSet();
	//			return DynamicData_set_get_member_status_impl(pPath, pNext, depth + 1, targetValue);
	//		}
	//	}
	//}

	return MemberStatus_None;
}

DynamicData_MemberStatus DynamicData_obj_get_member_status_impl(DynamicEditorPath* pPath, DynamicObject* pObject, u64 depth, DynamicData targetValue)
{
	bool isAtEnd = pPath->values.size() == depth;
	DynamicData currentValue = isAtEnd ? targetValue : pPath->values[depth];

	if (pObject->hPrototype)
	{
		u64 i;
		if (findValue(pObject->instantiated.values, &currentValue, &i))
		{
			if (isAtEnd)
			{
				return MemberStatus_Instantiated;
			}
			// already instantiated at this depth

			if (pObject->instantiated.values[i].type == DynamicData::Type_Object)
			{
				DynamicObject* pNext = pObject->instantiated.values[i].asObject();
				return DynamicData_obj_get_member_status_impl(pPath, pNext, depth + 1, targetValue);
			}
			else
			{
				return DynamicData_set_get_member_status_impl(pPath, pObject, pObject->instantiated.names[i], depth + 1, targetValue);
			}
		}
		else if (findValue(pObject->flattened.values, &currentValue, &i))
		{
			// if we enter any inhertied value we can early out.
			return MemberStatus_Inherited;
		}
	}
	else
	{
		u64 i;
		if (findValue(pObject->owned.values, &currentValue, &i))
		{
			if (isAtEnd)
			{
				return MemberStatus_Owned;
			}

			// Value is owned, nothing to do at this level. can continue
			if (pObject->owned.values[i].type == DynamicData::Type_Object)
			{
				DynamicObject* pNext = pObject->owned.values[i].asObject();
				return DynamicData_obj_get_member_status_impl(pPath, pNext, depth + 1, targetValue);
			}
			else
			{
				return DynamicData_set_get_member_status_impl(pPath, pObject, pObject->owned.names[i], depth + 1, targetValue);
			}
		}
	}

	return MemberStatus_None;
}

DynamicData_MemberStatus DynamicData_get_member_status(DynamicEditorPath* pPath, DynamicData value)
{
	DynamicObject* pCurrent = pPath->root;
	u64 depth = 0;
	return DynamicData_obj_get_member_status_impl(pPath, pCurrent, 0, value);
}

//DynamicData_MemberStatus DynamicData_get_member_status(DynamicData* pValue, u64 hName)
//{
//	if (DynamicObject* pObject = pValue->asObject())
//	{
//		if (pObject->hPrototype != 0)
//		{
//			u64 i;
//			if (findName(pObject->instantiated.names, hName, &i))
//			{
//				return MemberStatus_Instantiated;
//			}
//			if (findName(pObject->flattened.names, hName, &i))
//			{
//				return MemberStatus_Inherited;
//			}
//			return MemberStatus_None;
//		}
//		else
//		{
//			u64 i;
//			if (findName(pObject->owned.names, hName, &i))
//			{
//				return MemberStatus_Owned;
//			}
//		}
//	}
//
//	if (DynamicSet* pSet = pValue->asSet())
//	{
//		if (pSet->hPrototype != 0)
//		{
//			u64 i;
//			if (findName(pSet->added.ids, hName, &i))
//			{
//				return MemberStatus_Added;
//			}
//			if (findName(pSet->removed.ids, hName, &i))
//			{
//				return MemberStatus_Removed;
//			}
//			if (findName(pSet->instantiated.ids, hName, &i))
//			{
//				return MemberStatus_Instantiated;
//			}
//			return MemberStatus_None;
//		}
//		else
//		{
//			u64 i;
//			if (findName(pSet->owned.ids, hName, &i))
//			{
//				return MemberStatus_Owned;
//			}
//		}
//	}
//
//	return MemberStatus_None;
//}

void DynamicData_clone_internal(DynamicData* src, DynamicData* dst)
{
	switch (src->type)
	{
	case DynamicData::Type_Object:
	{
		*dst = DynamicData_obj_new();
		DynamicObject* pObject = dst->asObject();
		DynamicObject* pSrcObject = src->asObject();
		DynamicData_obj_before_read(pObject);
		DynamicData_obj_before_read(pSrcObject);
		u64 size = pSrcObject->flattened.values.size();

		pObject->owned.names.resize(size);
		pObject->owned.values.resize(size);

		for (u64 i = 0; i < size; ++i)
		{
			pObject->owned.names[i] = pSrcObject->flattened.names[i];
			DynamicData_clone_internal(&pSrcObject->flattened.values[i], &pObject->owned.values[i]);
		}

		break;
	}
	case DynamicData::Type_Set:
	{
		*dst = DynamicData_set_new();
		DynamicSet* pSet = dst->asSet();
		DynamicSet* pSrcSet = src->asSet();

		u64 size = pSrcSet->flattened.values.size();
		pSet->added.values.resize(size);

		for (u64 i = 0; i < size; ++i)
		{
			DynamicData_clone_internal(&pSrcSet->flattened.values[i], &pSet->added.values[i]);
		}

		break;
	}
	case DynamicData::Type_Integer:
	{
		*dst = DynamicData_int_new();
		dst->integer = src->integer;
		break;
	}
	case DynamicData::Type_Number:
	{
			
		*dst = DynamicData_num_new();
		dst->number = src->number;
		break;
	}
	case DynamicData::Type_String:
	{
		*dst = DynamicData_str_new();
		u64 capacity = src->string ? (strlen(src->string) + 1) : 0;
		if (capacity)
		{
			dst->string = (char*)malloc(capacity);
			strcpy_s(dst->string, capacity, src->string);
		}
		break;
	}
	case DynamicData::Type_Null:
		break;
	}
}

DynamicData DynamicData_clone(DynamicData* src)
{
	DynamicData value;
	DynamicData_clone_internal(src, &value);
	return value;
}


DynamicData DynamicData_obj_find_impl(DynamicObject* pObject, u64 hName)
{
	DynamicData_obj_before_read(pObject);
	u64 i;
	if (findName(pObject->flattened.names, hName, &i))
	{
		return pObject->flattened.values[i];
	}

	return DynamicData_make_null();
}

DynamicData DynamicData_obj_find(DynamicData* pValue, u64 hName)
{
	if (pValue->type == DynamicData::Type_Object)
	{
		DynamicObject* pObject = pValue->asObject();
		DynamicData_obj_before_read(pObject);
		return DynamicData_obj_find_impl(pObject, hName);
	}

	return DynamicData_make_null();
}

void DynamicData_assign_root(DynamicData* newRoot, DynamicData* value)
{
	u64 rootId = newRoot->id();

	if (rootId)
	{
		if (DynamicObject* pObject = value->asObject())
		{
			pObject->hRoot = newRoot->id();
		}
	}
}

void DynamicData_obj_add(DynamicData* target, u64 hName, DynamicData add)
{
	if (DynamicObject* pObject = target->asObject())
	{
		pObject->version++;

		for (i32 i = 0; i < pObject->owned.names.size(); ++i)
		{
			if (hName == pObject->owned.names[i])
			{
				// todo: api add existing is error?
				pObject->owned.values[i] = add;
				return;
			}
		}

		pObject->owned.names.push_back(hName);
		pObject->owned.values.push_back(add);

		DynamicData_assign_root(target, &add);
	}
}

void DynamicData_obj_set(DynamicData* object, u64 hName, DynamicData value)
{
	if (DynamicObject* pObject = object->asObject())
	{
		DynamicData_obj_before_read(pObject);

		if (pObject->hPrototype == 0)
		{
			for (i32 i = 0; i < pObject->owned.names.size(); ++i)
			{
				if (hName == pObject->owned.names[i])
				{
					pObject->owned.values[i] = value;
					pObject->version++;
					return;
				}
			}
			assert(false && "didnt find key");
		}
		else
		{
			u64 i;
			if (findName(pObject->overrides.names, hName, &i))
			{
				// todo remove the previous edits if necessary

				// Its currently an edit, re-assign the edit
				pObject->overrides.values[i] = value;
			}
			else
			{
				pObject->overrides.names.push_back(hName);
				pObject->overrides.values.push_back(value);
			}

			if (findName(pObject->flattened.names, hName, &i))
			{
				pObject->flattened.values[i] = value;
			}
			else
			{
				assert(false && "Value override should already exist in flattened when overriding it");
			}

			pObject->version++;
			return;
		}
	}

	assert(false && "didnt find key");
}

//void DynamicData_add_to_set(DynamicData* object, u64 hMember, DynamicData item)
//{
//	if (DynamicObject* pObject = object->asObject())
//	{
//		DynamicData_obj_before_read(pObject);
//		
//		if (pObject->hPrototype == 0)
//		{
//			u64 i;
//			if (findName(pObject->owned.names, hMember, &i))
//			{
//				if (DynamicSet* pSet = pObject->owned.values[i].asSet())
//				{
//					pSet->owned.ids.push_back(item.id());
//					pSet->owned.values.push_back(item);
//
//					pSet->flattened.ids.push_back(item.id());
//					pSet->flattened.values.push_back(item);
//
//					pSet->version++;
//					pObject->version++;
//					return;
//				}
//
//				assert(false && "Type mismatch");
//				return;
//			}
//			assert(false && "didnt find key");
//		}
//		else
//		{
//			u64 i;
//			if (findName(pObject->instantiated.names, hMember, &i))
//			{
//				DynamicSet* pSet = pObject->instantiated.values[i].asSet();
//				if (findName(pSet->added.ids, item.id(), &i))
//				{
//					assert(false && "we shouldnt add the same item twice");
//				}
//
//				pSet->added.ids.push_back(item.id());
//				pSet->added.values.push_back(item);
//
//				pSet->flattened.ids.push_back(item.id());
//				pSet->flattened.values.push_back(item);
//
//				pSet->version++;
//				pObject->version++;
//			}
//			else
//			{
//				DYNAMIC_DATA_ERROR("need to instantiate set/member does not exist");
//			}
//		}
//	}
//}



//void _DynamicData_obj_arr_push(DynamicData* object, u64 hArrayName, DynamicData value)
//{
//	if (DDObject* pObject = object->asObject())
//	{
//		DynamicData_obj_before_read(pObject);
//
//		if (pObject->hPrototype == 0)
//		{
//			for (i32 i = 0; i < pObject->owned.names.size(); ++i)
//			{
//				if (hArrayName == pObject->owned.names[i])
//				{
//					pObject->owned.values[i].asArray()->values.push_back(value);
//					pObject->version++;
//					return;
//				}
//			}
//
//			assert(false && "didnt find key");
//		}
//		else
//		{
//			DynamicEdit edit;
//			edit.type = DynamicEdit::Type_ArrayAppend;
//			edit.arrayAppend.value = value;
//
//			u64 i;
//			if (findName(pObject->edits.names, hArrayName, &i))
//			{
//				pObject->edits.edits[i].push_back(edit);
//			}
//			else
//			{
//				pObject->edits.names.push_back(hArrayName);
//				pObject->edits.edits.push_back({});
//				pObject->edits.edits.back().push_back(edit);
//			}
//
//			if (findName(pObject->flattened.names, hArrayName, &i))
//			{
//				pObject->flattened.dirty = true;
//				//pObject->flattened.values[i].asArray()->values.push_back(value);
//			}
//			else
//			{
//				assert(false && "didnt find key");
//				// we push to array but it doesnt exist?
//			}
//			pObject->version++;
//			return;
//		}
//	}
//	assert(false && "didnt find key");
//}


void DynamicData_obj_set_add(DynamicData* object, u64 hSet, DynamicData value)
{

}

void DynamicData_array_pop(DynamicData* array, DynamicData value)
{

}

void DynamicData_obj_compose(DynamicObject* pObject, eastl::vector<u64>& names, eastl::vector<DynamicData>& values)
{
	DynamicObject* pPrototype = pObject->hPrototype != 0 ? lookup_obj(pObject->hPrototype) : nullptr;

	while (pPrototype && pPrototype->tombstone)
	{
		pObject->hPrototype = pPrototype->hPrototype;
		pPrototype = lookup_obj(pObject->hPrototype);
	}

	// evaluate prototype
	if (!pPrototype)
	{
		// todo remove
		pObject->flattened.names = pObject->owned.names;
		pObject->flattened.values = pObject->owned.values;

		names = pObject->owned.names;
		values = pObject->owned.values;
		pObject->flattened.dirty = false;

		return;
	}
	if (pPrototype)
	{
		if (DDObject_is_up_to_date(pObject, pPrototype))
		{
			// todo if we have no edits at top level we can directly reference the arrays
			names = pObject->flattened.names;
			values = pObject->flattened.values;
			return;
		}
		DynamicData_obj_compose(pPrototype, names, values);
		pObject->flattened.basedOnVersion = pPrototype->version;
	}

	// Redirect instances and assign flattened state
	// TODO SOON is the logic correct?
	for (u64 i = 0; i < pObject->instantiated.names.size(); ++i)
	{
		u64 hName = pObject->instantiated.names[i];
		
		u64 prototypeIndex;
		// for an instance we need to redirect the values to our instance. applying the instance edits.
		// todo if its no longer in prototype clean it up
		if (findName(names, hName, &prototypeIndex))
		{
			//assert(values[prototypeIndex].type == DynamicData::Type_Object);
			//assert(pObject->instantiated.values[i].type == DynamicData::Type_Object);

			if (values[prototypeIndex].type == DynamicData::Type_Object)
			{
				values[prototypeIndex].hObject = pObject->instantiated.values[i].hObject;
			}

			//if (values[prototypeIndex].type == DynamicData::Type_Set)
			//{
			//	values[prototypeIndex].hSet = pObject->instantiated.values[i].hSet;
			//}
		}
	}

	// apply edits
	for (u64 i = 0; i < pObject->overrides.names.size(); ++i)
	{
		u64 hName = pObject->overrides.names[i];
		DynamicData override = pObject->overrides.values[i];
	}

	if (pObject->hPrototype != 0)
	{
		pObject->flattened.names = names;
		pObject->flattened.values = values;
	}

	pObject->flattened.dirty = false;
}

bool DynamicSet_is_up_to_date(DynamicObject* object, u64 hSetMember)
{
	return false;

	/*if (pSet->flattened.basedOnVersion == pPrototype->version && !pSet->flattened.dirty && !pPrototype->flattened.dirty)
	{
		if (pPrototype->hPrototype != 0)
		{
			DynamicSet* pNextPrototype = lookup_set(pPrototype->hPrototype);
			return DynamicSet_is_up_to_date(pSet, pNextPrototype);
		}
		else
		{
			return true;
		}
	}
	return false;*/
}

void DynamicData_set_compose(DynamicObject* pObject, u64 hMember, eastl::vector<u64>& ids, eastl::vector<DynamicData>& values)
{
	DynamicObject* pPrototype = pObject->hPrototype != 0 ? lookup_obj(pObject->hPrototype) : nullptr;

	DynamicSet* pSet = DynamicData_obj_find_impl(pObject, hMember).asSet();
	DynamicSet* pPrototypeSet = pPrototype ? DynamicData_obj_find_impl(pPrototype, hMember).asSet() : nullptr;

	// evaluate prototype
	if (!pPrototypeSet)
	{
		// todo remove these allocation/copies
		pSet->flattened.values = pSet->added.values;
		values = pSet->added.values;

		for (auto& d : pSet->added.values)
			ids.push_back(d.id());

		pSet->flattened.dirty = false;
		return;
	}
	if (pPrototype)
	{
		//if (DynamicSet_is_up_to_date(pSet, pPrototype))
		//{
		//	// todo if we have no edits at top level we can directly reference the arrays
		//	values = pSet->flattened.values;
		//	for (auto& d : pSet->flattened.values)
		//		ids.push_back(d.id());

		//	pSet->flattened.dirty = false;

		//	return;
		//}
		DynamicData_set_compose(pPrototype, hMember, ids, values);
		pSet->flattened.basedOnVersion = pPrototype->version;
	}

	// Redirect instances and assign flattened state
	// TODO SOON is the logic correct?
	for (u64 i = 0; i < pSet->instantiated.ids.size(); ++i)
	{
		u64 originalId = pSet->instantiated.ids[i];

		u64 index;
		// for an instance we need to redirect the values to our instance. applying the instance edits.
		// todo if its no longer in prototype clean it up
		if (findId(ids, originalId, &index))
		{
			if (values[index].type == DynamicData::Type_Object)
			{
				ids[index] = pSet->instantiated.values[i].id();
				values[index].hObject = pSet->instantiated.values[i].hObject;
			}
		}
	}

	// apply edits
	for (u64 i = 0; i < pSet->added.values.size(); ++i)
	{
		u64 id = pSet->added.values[i].id();
		DynamicData value = pSet->added.values[i];
		ids.push_back(id);
		values.push_back(value);
	}

	for (u64 i = 0; i < pSet->removed.values.size(); ++i)
	{
		u64 id = pSet->removed.values[i].id();
		u64 index;
		if (findId(ids, id, &index))
		{
			ids.erase(ids.begin() + index);
			values.erase(values.begin() + index);
		}
	}

	pSet->flattened.values = values;
	pSet->flattened.dirty = false;
}

void DynamicData_obj_before_read(DynamicObject* pObject)
{
	eastl::vector<u64> names;
	eastl::vector<DynamicData> values;
	DynamicData_obj_compose(pObject, names, values);
}

void DynamicData_set_before_read(DynamicObject* pObject, u64 hMember)
{
	eastl::vector<u64> ids;
	eastl::vector<DynamicData> values;
	DynamicData_set_compose(pObject, hMember, ids, values);
}

void DynamicData_set_before_read(DynamicData* pValue, u64 hMember)
{
	eastl::vector<u64> ids;
	eastl::vector<DynamicData> values;

	DynamicObject* pObject = pValue->asObject();
	DynamicData_set_compose(pObject, hMember, ids, values);
}

u64 string_repository_hash(const char* str)
{
	u64 key = MetroHash64::HashStr(str);
	auto find = s_string_repository.find(key);

	if (find == s_string_repository.end())
	{
		s_string_repository[key] = str;
	}

	return key;
}

const char* string_repository_get(u64 hName)
{
	auto find = s_string_repository.find(hName);

	if (find != s_string_repository.end())
	{
		return find->second;
	}

	return "Invalid String";
}

const char* lookup_name(u64 hName)
{
	return string_repository_get(hName);
}

void DynamicData_registerParser(u64 hType, DynamicDataParser_i parser)
{
	g_parsers[hType] = parser;
}

DDEntity DynamicData_readEntity(DynamicData* pEntity)
{
	DDEntity entity{};

	u64 hType = ENTITY_TYPE_ID;

	DynamicDataParser_i* parser = lookup_parser(hType);

	if (parser)
	{
		parser->parse(pEntity, &entity);
	}

	return entity;
}

DDTransformComponent DynamicData_readTransform(DynamicData* pEntity)
{
	DDTransformComponent component{};

	u64 hTypeId = MetroHash64::HashStr(s_typeIdKey);
	u64 hComponents = MetroHash64::HashStr("components");

	/*if (DDArray* pComponents = DynamicData_obj_find(pEntity, hComponents).asArray())
	{
		for (DynamicData& comp : pComponents->values)
		{
			u64 comp_type = DynamicData_obj_find(&comp, hTypeId).asUint();
			if (comp_type == COMPONENT_ID_TRANSFORM)
			{
				DynamicDataParser_i* parser = lookup_parser(COMPONENT_ID_TRANSFORM);

				if (parser)
				{
					parser->parse(pEntity, &component);
				}
			}
		}
	}*/

	return component;
}

void DynamicData_writeBack(DynamicData* pData, void* pValue)
{
	u64 hTypeId = MetroHash64::HashStr(s_typeIdKey);

	DynamicData type = DynamicData_obj_find(pData, hTypeId);

	DynamicDataParser_i* parser = lookup_parser(type.asUint());

	if (parser)
	{
		parser->write_back(pData, pValue);
	}
}

void registerEntityTemplate()
{
	DynamicData root = DynamicData_obj_new();

	u64 hTypeName = string_repository_hash(s_typeNameKey);
	u64 hFields = string_repository_hash(s_fieldsKey);

	DynamicData entityName = DynamicData_make_str("entity");
	DynamicData_obj_add(&root, hTypeName, entityName);


	DynamicData field_children = DynamicData_obj_new();
	DynamicData_obj_add(&field_children, string_repository_hash("name"), DynamicData_make_str("Unnamed Entity"));
	DynamicData_obj_add(&field_children, string_repository_hash("children"), DynamicData_set_new());
	DynamicData_obj_add(&field_children, string_repository_hash("components"), DynamicData_set_new());

	DynamicData entity_subobject = DynamicData_obj_new();
	DynamicData_obj_add(&entity_subobject, string_repository_hash("sub-float"), DynamicData_num_new());
	DynamicData_obj_add(&entity_subobject, string_repository_hash("sub-int"), DynamicData_num_new());

	DynamicData nested_subobject = DynamicData_obj_new();
	DynamicData_obj_add(&nested_subobject, string_repository_hash("nested-float"), DynamicData_num_new());
	DynamicData_obj_add(&nested_subobject, string_repository_hash("nested-int"), DynamicData_num_new());
	DynamicData_obj_add(&entity_subobject, string_repository_hash("nested-subobject"), nested_subobject);

	DynamicData_obj_add(&field_children, string_repository_hash("subobject"), entity_subobject);

	DynamicData_obj_add(&root, hFields, field_children);

	DynamicDataParser_i parser;

	parser.parse = [](DynamicData* value, void* target)
	{
		DDEntity* pEntity = (DDEntity*)target;

		u64 hNameField = string_repository_hash("name");
		u64 hChildrenField = string_repository_hash("children");
		u64 hComponentsField = string_repository_hash("components");
		
		pEntity->name = DynamicData_obj_find(value, hNameField).asString();

		DynamicSet* pChildren = DynamicData_obj_find(value, hChildrenField).asSet();

		pEntity->children = pChildren->flattened.values;

		DynamicSet* pComponents = DynamicData_obj_find(value, hComponentsField).asSet();
		pEntity->components = pComponents->flattened.values;
	};

	parser.write_back = [](DynamicData* value, void* source)
	{
		DDEntity* pEntity = (DDEntity*)source;

		if ((pEntity->editedMask & DDEntity::FieldMask_Name) != 0)
		{
			u64 hNameField = MetroHash64::HashStr("name");
			DynamicData_obj_set(value, hNameField, DynamicData_make_str(pEntity->name));
		}

		if ((pEntity->editedMask & DDEntity::FieldMask_Children) != 0)
		{
			//u64 hChildrenField = MetroHash64::HashStr("children");
			//DynamicData setChildren = DynamicData_set_new();
			////arrChildren.pArray->values = pEntity->children;
			//DynamicData_obj_set(value, hChildrenField, setChildren);
		}

		if ((pEntity->editedMask & DDEntity::FieldMask_Components) != 0)
		{
			//u64 hComponentsField = MetroHash64::HashStr("components");
			//DynamicData arrComponents = DynamicData_set_new();
			////arrComponents.pArray->values = pEntity->components;
			//DynamicData_obj_set(value, hComponentsField, arrComponents);
		}
	};

	DynamicData_registerParser(ENTITY_TYPE_ID, parser);

	g_templates[ENTITY_TYPE_ID] = root;
}

void registerComponent_TransformTemplate()
{
	DynamicData root = DynamicData_obj_new();

	u64 hTypeName = string_repository_hash(s_typeNameKey);
	u64 hFields = string_repository_hash(s_fieldsKey);

	DynamicData componentName = DynamicData_make_str("component_transform");
	DynamicData_obj_add(&root, hTypeName, componentName);

	DynamicData field_children = DynamicData_obj_new();

	DynamicData_obj_add(&field_children, string_repository_hash("x"), DynamicData_num_new());
	DynamicData_obj_add(&field_children, string_repository_hash("y"), DynamicData_num_new());
	DynamicData_obj_add(&field_children, string_repository_hash("z"), DynamicData_num_new());

	DynamicData_obj_add(&root, hFields, field_children);

	DynamicDataParser_i parser;

	parser.parse = [](DynamicData* value, void* data)
	{
		DDTransformComponent* pComponent = (DDTransformComponent*)data;

		u64 hXField = MetroHash64::HashStr("x");
		u64 hYField = MetroHash64::HashStr("y");
		u64 hZField = MetroHash64::HashStr("z");

		pComponent->x = (f32)DynamicData_obj_find(value, hXField).asNumber();
		pComponent->y = (f32)DynamicData_obj_find(value, hYField).asNumber();
		pComponent->z = (f32)DynamicData_obj_find(value, hZField).asNumber();
	};

	parser.write_back = [](DynamicData* value, void* data)
	{
		DDTransformComponent* pComponent = (DDTransformComponent*)data;

		if ((pComponent->editedMask & DDTransformComponent::FieldMask_X) != 0)
		{
			u64 hXField = MetroHash64::HashStr("x");
			DynamicData_obj_set(value, hXField, DynamicData_make_num(pComponent->x));
		}

		if ((pComponent->editedMask & DDTransformComponent::FieldMask_Y) != 0)
		{
			u64 hYField = MetroHash64::HashStr("y");
			DynamicData_obj_set(value, hYField, DynamicData_make_num(pComponent->y));
		}

		if ((pComponent->editedMask & DDTransformComponent::FieldMask_Z) != 0)
		{
			u64 hZField = MetroHash64::HashStr("z");
			DynamicData_obj_set(value, hZField, DynamicData_make_num(pComponent->z));
		}
	};

	DynamicData_registerParser(COMPONENT_ID_TRANSFORM, parser);
	g_templates[COMPONENT_ID_TRANSFORM] = root;
}

void registerComponent_ColorTemplate()
{
	DynamicData root = DynamicData_obj_new();

	u64 hTypeName = string_repository_hash(s_typeNameKey);
	u64 hFields = string_repository_hash(s_fieldsKey);

	DynamicData componentName = DynamicData_make_str("component_color");
	DynamicData_obj_add(&root, hTypeName, componentName);

	DynamicData field_children = DynamicData_obj_new();

	DynamicData color = DynamicData_obj_new();
	
	DynamicData_obj_add(&color, string_repository_hash("r"), DynamicData_num_new());
	DynamicData_obj_add(&color, string_repository_hash("g"), DynamicData_num_new());
	DynamicData_obj_add(&color, string_repository_hash("b"), DynamicData_num_new());

	DynamicData_obj_add(&field_children, string_repository_hash("color"), color);

	DynamicData_obj_add(&root, hFields, field_children);

	g_templates[COMPONENT_ID_COLOR] = root;
}


DynamicData* DynamicData_get_template(u64 id)
{
	return &g_templates[id];
}

DynamicData DynamicData_createFromTemplate(u64 hTemplate)
{
	DynamicData value = DynamicData_obj_new();

	DynamicData* pTemplate = DynamicData_get_template(hTemplate);
	
	u64 hType = string_repository_hash(s_typeNameKey);
	u64 hTypeId = string_repository_hash(s_typeIdKey);

	const char* typeName = DynamicData_obj_find(pTemplate, hType).asString();

	DynamicData_obj_add(&value, hType, DynamicData_make_str(typeName));
	DynamicData_obj_add(&value, hTypeId, DynamicData_make_int((i64)hTemplate));

	u64 hFields = string_repository_hash(s_fieldsKey);

	if (DynamicObject* fields = DynamicData_obj_find(pTemplate, hFields).asObject())
	{
		u64 numFields = fields->owned.values.size();
		for (u64 i = 0; i < numFields; ++i)
		{
			DynamicData valueToAdd;
			DynamicData_clone_internal(&fields->owned.values[i], &valueToAdd);
			DynamicData_obj_add(&value, fields->owned.names[i], valueToAdd);
		}
	}

	return value;
}

void DynamicData_view(DynamicData* pData)
{
	DynamicEditorPath path;
	path.root = pData->asObject();
	DynamicData_view_draw_object(pData, &path);
	//DynamicData_view_impl(pData, 0ull, &path);
}

Entity* Entity::create(Allocator* a)
{
	Entity* entity = alloc<Entity>(a);
	entity->children.set_allocator(a);
	entity->instantiatedRoots.set_allocator(a);
	sprintf_s(entity->name, "New Entity (%d)", s_nextId++);

	return entity;
}

Entity* Entity::createFromPrototype(Allocator* a, truth::Key prototype)
{
	Entity* entity = alloc<Entity>(a);
	const Entity* prototypeEntity  = (const Entity*)g_truth->read(g_truth->snap(), prototype);

	entity->position = prototypeEntity->position;
	entity->prototype = prototype;
	entity->position.inheritsX = true;
	entity->position.inheritsY = true;
	entity->position.inheritsZ = true;

	entity->children.set_allocator(a);
	entity->instantiatedRoots.set_allocator(a);
	sprintf_s(entity->name, "Instance of prototype (%s) ", prototypeEntity->name);

	return entity;
}


TruthElement* Entity::clone(Allocator* a) const
{
	Entity* entityClone = alloc<Entity>(a);
	entityClone->root = root;

	entityClone->children = children.clone();
	entityClone->instantiatedRoots = instantiatedRoots.clone();
	entityClone->position = position;
	entityClone->prototype = prototype;

	memcpy(entityClone->name, name, sizeof(name));
	return entityClone;
}
