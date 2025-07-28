#include "Entity.h"

#include <algorithm>
#include <stdio.h>

#include "Editor.h"
#include "imgui.h"
#include "murmurhash.inl"


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


void PushStatusStyle(MemberStatus status)
{
	constexpr u32 COLOR_OWNED = IM_COL32(255, 255, 255, 255);
	constexpr u32 COLOR_INHERIT = IM_COL32(100, 100, 100, 255);
	constexpr u32 COLOR_INSTANTIATED = IM_COL32(255, 255, 180, 255);
	constexpr u32 COLOR_OVERRIDDEN = IM_COL32(180, 180, 255, 255);
	constexpr u32 COLOR_REMOVED = IM_COL32(255, 180, 180, 255);
	constexpr u32 COLOR_ERROR = IM_COL32(255, 0, 0, 255);

	u32 styles[]
	{
		COLOR_OWNED,
		COLOR_INHERIT,
		COLOR_INSTANTIATED,
		COLOR_OVERRIDDEN,
		COLOR_REMOVED,
		COLOR_ERROR,
	};

	ImGui::PushStyleColor(ImGuiCol_Text, styles[(int)status]);
}

void PopStatusStyle()
{
	ImGui::PopStyleColor();
}

struct DynamicEditorPath
{
	DynamicObject* root;
	eastl::vector<DynamicData> values;
};


struct DynamicObject
{
	u64 id;
	u64 hRoot;
	u64 hType;

	DynamicData prototype;
	bool tombstone;

	struct Members
	{
		eastl::vector<u64> names;
		eastl::vector<DynamicData> values;
		eastl::vector<MemberStatus> statuses;
	};

	Members members;
	u64 version;
};

struct DynamicSet
{
	struct Added
	{
		eastl::vector<DynamicData> values;
	};

	struct Removed
	{
		eastl::vector<DynamicData> values;
	};

	struct Instantiated
	{
		eastl::vector<u64> ids;
		eastl::vector<DynamicData> values;
	};

	Added added;
	Removed removed;
	Instantiated instantiated;
};


DynamicObjectDebugView DynamicData_DebugExpression(u64 hObject)
{
	DynamicObjectDebugView debugData;
	if (DynamicObject* pObject = lookup_obj(hObject))
	{
		for (u64 i = 0; i < pObject->members.names.size(); ++i)
		{
			DebugValuePair& pair = debugData.values.emplace_back();
			pair.name = string_repository_get(pObject->members.names[i]);
			pair.value = pObject->members.values[i];
			pair.status = pObject->members.statuses[i];
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
		buf.write("Object[%d]", value.asObject()->members.values.size());
		break;
	case DynamicData::Type_Set:
	{
		buf.write("s", "Set");
		break;
	}
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

bool findId(const eastl::vector<u64>& vecIds, u64 id, u64* outIndex)
{
	for (u64 i = 0; i < vecIds.size(); ++i)
	{
		if (id == vecIds[i])
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

void DynamicData_view_draw_object_id(DynamicObject* pObject)
{
	if (pObject->prototype.id() == 0)
	{
		if (ImGui::TreeNodeEx(Printf("Object ID : %llu", pObject->id), ImGuiTreeNodeFlags_Leaf))
		{
			ImGui::TreePop();
		}
	}
	else
	{
		if (ImGui::TreeNodeEx(Printf("Object ID : %llu [Prototype ID : %llu]", pObject->id, pObject->prototype.id()), ImGuiTreeNodeFlags_Leaf))
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

void DynamicData_view_object_context_menu(DynamicData* pValue, u64 hMember, DynamicEditorPath* pPath, bool parentInherited)
{
	if (ImGui::IsItemClicked(1))
	{
		ImGui::OpenPopup("dd_view_context_menu");
	}

	if (ImGui::BeginPopup("dd_view_context_menu"))
	{
		DynamicData member = DynamicData_obj_get(pValue, hMember);
		MemberStatus status = DynamicData_get_member_status(pValue, hMember);

		if (!parentInherited && status == MemberStatus::Overridden)
		{
			if (ImGui::MenuItem("Clear value"))
			{
				DynamicData_obj_clear_override(pValue, hMember);
			}
		}

		if (ImGui::MenuItem("Create instance of"))
		{
			DynamicData instance = DynamicData_new_from_prototype(&member);

			constexpr u64 hNameField = TM_STATIC_HASH("name", 0xd4c943cba60c270bULL);
			DynamicData name = DynamicData_obj_get(&member, hNameField);
			if (name.type == DynamicData::Type_String)
			{
				DynamicData_obj_set(&instance, hNameField, DynamicData_make_str(Printf("Instance of [%s]", name.asString())));
			}
			Debug_register_root_object(instance);
		}


		if (!parentInherited && status == MemberStatus::Inherited)
		{
			if (ImGui::MenuItem("Instantiate subobject"))
			{
				DynamicData_instantiate_subobject(&pPath->values.back(), hMember);
			}
		}


		//ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
		//ImGui::MenuItem(Printf("Member status : %s", to_string(memberStatus)));

		//ImGui::BeginDisabled(memberStatus != MemberStatus_Inherited);
		//if (ImGui::MenuItem("Instantiate object"))
		//{
		//	//DynamicData_instantiate_path(pPath, member);
		//}
		//ImGui::EndDisabled();

		//ImGui::BeginDisabled(memberStatus != MemberStatus_Instantiated);
		//if (ImGui::MenuItem("Revert to prototype"))
		//{
		//	//DynamicData_instantiate_clear(&value, hMemberName);
		//}
		//ImGui::EndDisabled();

		//ImGui::PopStyleVar();
		ImGui::EndPopup();
	}
}

void DynamicData_view_impl(DynamicData* parent, u64 hMember, DynamicEditorPath* pPath, bool isInherited);

void DynamicData_view_object_set_context_menu(DynamicData* pValue, u64 hMember, DynamicEditorPath* pPath, bool parentInherited)
{

}


void DynamicData_view_draw_root_object(DynamicData* pRoot)
{
	
}


void DynamicData_view_draw_object(DynamicData* pValue, DynamicEditorPath* pPath, bool parentInherited, bool* outOpenCtxMenu)
{
	DynamicObject* pObject = pValue->asObject();

	for (u64 i = 0; i < pObject->members.names.size(); ++i)
	{
		u64 hMemberName = pObject->members.names[i];

		bool isContainer = pObject->members.values[i].isContainer();
		MemberStatus memberStatus = pObject->members.statuses[i];
		memberStatus = parentInherited ? MemberStatus::Inherited : memberStatus;

		if (isContainer)
		{
			pPath->values.push_back(pObject->members.values[i]);
		}

		const char* memberName = string_repository_get(hMemberName);

		if (!isContainer)
		{
			Printf buf;
			DynamicData element = DynamicData_obj_get(pValue, hMemberName);
			DynamicData_format_value(buf, element);
			PushStatusStyle(memberStatus);

			if (ImGui::TreeNodeEx(Printf("%s : %s", memberName, buf.cstr()), ImGuiTreeNodeFlags_Leaf))
			{
				ImGui::TreePop();
			}
			PopStatusStyle();

			if (element.type == DynamicData::Type_Number && ImGui::IsItemClicked() && !parentInherited)
			{
				DynamicData newPos = DynamicData_make_num(element.asNumber() + 1);
				DynamicData_obj_set(pValue, hMemberName, newPos);
			}
		}
		else
		{
			PushStatusStyle(memberStatus);
			bool childOpen = ImGui::TreeNode(memberName);
			*outOpenCtxMenu = ImGui::IsItemClicked(1);
			PopStatusStyle();

			if (childOpen)
			{
				DynamicData_view_impl(pValue, hMemberName, pPath, memberStatus == MemberStatus::Inherited);
				ImGui::TreePop();
			}

			pPath->values.pop_back();
		}
	}
}

void DynamicData_view_draw_object_set(DynamicData* pValue, u64 hSetName, DynamicEditorPath* pPath, bool parentInherited)
{
	eastl::vector<DynamicData> members = DynamicData_get_subobject_set(pValue, hSetName);
	eastl::vector<DynamicData> removed = DynamicData_get_subobject_set_locally_removed(pValue, hSetName);

	DynamicSet* pSet = DynamicData_obj_get(pValue, hSetName).asSet();

	const char* setName = string_repository_get(hSetName);

	for (u64 i = 0; i< members.size(); ++i)
	{
		
	}

	for (u64 i = 0; i < removed.size(); ++i)
	{
		DynamicData* item = &removed[i];
		ImGui::PushID((int)item->id());
		PushStatusStyle(MemberStatus::Removed);
		ImVec2 cursorPos = ImGui::GetCursorScreenPos();

		Printf text = Printf("%s [%d]", setName, (int)i);

		if (ImGui::TreeNodeEx(text.cstr(), ImGuiTreeNodeFlags_Leaf))
		{
			ImGui::TreePop();
		}

		ImVec2 textSize = ImGui::CalcTextSize(text.cstr());
	
		ImVec2 start = cursorPos;
		ImVec2 end = ImVec2(cursorPos.x + textSize.x, cursorPos.y);

		// Adjust Y position to draw the line through the middle of the text
		float textHeight = textSize.y;
		start.y += textHeight * 0.5f;
		end.y += textHeight * 0.5f;

		// Draw the strikethrough line
		ImGui::GetWindowDrawList()->AddLine(start, end, IM_COL32(255, 255, 255, 255), 1.0f);

		PopStatusStyle();
		ImGui::PopID();
	}
}

void DynamicData_view_impl(DynamicData* parent, u64 hMember, DynamicEditorPath* pPath, bool isInherited)
{
	DynamicData value = DynamicData_obj_get(parent, hMember);
	MemberStatus status = DynamicData_get_member_status(parent, hMember);

	status = isInherited ? MemberStatus::Inherited : status;

	switch (value.type)
	{
	case DynamicData::Type_Object:
	{
		pPath->values.push_back(*parent);
		bool rclick = false;
		DynamicData_view_draw_object(&value, pPath, isInherited, &rclick);
		pPath->values.pop_back();

		if (rclick)
		{
			DynamicData_view_object_context_menu(parent, hMember, pPath, isInherited);
		}
		break;
	}
	case DynamicData::Type_Set:
	{
		pPath->values.push_back(*parent);
		DynamicData_view_draw_object_set(parent, hMember, pPath, isInherited);
		pPath->values.pop_back();
		break;
	}
	case DynamicData::Type_Integer:
	case DynamicData::Type_Number:
	case DynamicData::Type_String:
	case DynamicData::Type_Null:
	{
		Printf buf;
		PushStatusStyle(status);
		DynamicData_format_value(buf, value);
		bool r = ImGui::TreeNodeEx(buf.cstr(), ImGuiTreeNodeFlags_Leaf);
		PopStatusStyle();
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

DynamicData DynamicData_instance_new(DynamicData* pPrototype)
 {
	DynamicData value = DynamicData_make_null();

	if (pPrototype->type == DynamicData::Type_Object)
	{
		DynamicObject* pObject = new DynamicObject();

		DynamicObject* pProtoObject = pPrototype->asObject();

		u64 size = pProtoObject->members.names.size();
		pObject->members.names = pProtoObject->members.names;
		pObject->members.values = pProtoObject->members.values;
		pObject->members.statuses.resize(size, MemberStatus::Inherited);

		u64 id = next_obj_id();
		value.type = DynamicData::Type_Object;
		value.hObject = id;
		pObject->prototype = *pPrototype;
		pObject->version = 1;

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
	u64 i;
	DynamicData created = DynamicData_make_null();
	if (findName(pObject->members.names, hName, &i))
	{
		if (pObject->members.statuses[i] == MemberStatus::Inherited)
		{
			DynamicData* pPrototype = &pObject->members.values[i];

			created = DynamicData_new_from_prototype(pPrototype);
			pObject->members.values[i] = created;
			pObject->members.statuses[i] = MemberStatus::Instantiated;
		}
		else
		{
			DYNAMIC_DATA_ERROR("Instantiate something that is not inherited is not valid");
		}
	}

	return created;
}

DynamicData DynamicData_instantiate_subobject(DynamicData* pValue, u64 hName)
{
	DynamicData created = DynamicData_make_null();

	if (DynamicObject* pObject = pValue->asObject())
	{
		u64 i;
		if (findName(pObject->members.names, hName, &i))
		{
			if (pObject->members.statuses[i] == MemberStatus::Inherited)
			{
				DynamicData* pPrototype = &pObject->members.values[i];

				created = DynamicData_new_from_prototype(pPrototype);
				pObject->members.values[i] = created;
				pObject->members.statuses[i] = MemberStatus::Instantiated;
			}
			else
			{
				DYNAMIC_DATA_ERROR("Instantiate something that is not inherited is not valid");
			}
		}
	}

	return created;
}

void DynamicData_clear_instantiated_subobject(DynamicData* pValue, u64 hName)
{
	if (pValue->type == DynamicData::Type_Object)
	{
		DynamicObject* pObject = pValue->asObject();
		u64 i;
		if (findName(pObject->members.names, hName, &i))
		{
			if (pObject->members.statuses[i] == MemberStatus::Instantiated)
			{
				pObject->members.values[i] = pObject->members.values[i].asObject()->prototype;
				pObject->members.statuses[i] = MemberStatus::Inherited;
			}
		}
	}
}

DynamicData DynamicData_set_new()
{
	DynamicData value;

	DynamicSet* pSet = new DynamicSet();
	value.type = DynamicData::Type_Set;
	value.pSet = new DynamicSet();

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
		if (pObject->prototype.id() == 0)
		{
			DYNAMIC_DATA_ERROR("Instantiate only valid on objects with a prototype");
		}

		DynamicData prototype = pObject->prototype;

		eastl::vector<DynamicData> prototypeSet = DynamicData_get_subobject_set(&prototype, hSetMember);

		u64 i;
		if (findName(pObject->members.names, hSetMember, &i))
		{
			if (pObject->members.statuses[i] == MemberStatus::Instantiated)
			{
				if (DynamicSet* pSet = pObject->members.values[i].asSet())
				{
					if (findId(pSet->instantiated.ids, item.id(), &i))
					{
						DYNAMIC_DATA_ERROR("value is already instantiated");
					}
					else if (findValue(pSet->added.values, &item, &i))
					{
						DynamicData prototype = pSet->added.values[i];
						DynamicData instantiated = DynamicData_new_from_prototype(&prototype);
						pSet->instantiated.ids.push_back(item.id());
						pSet->instantiated.values.push_back(instantiated);
					}
					else
					{
						DYNAMIC_DATA_ERROR("value not present in set");
					}
				}
				else
				{
					DYNAMIC_DATA_ERROR("set is not instantiated");
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
		if (findName(pObject->members.names, hSetMember, &i))
		{
			MemberStatus membersStatus = pObject->members.statuses[i];
			if (membersStatus == MemberStatus::Instantiated || membersStatus == MemberStatus::Owned)
			{
				if (DynamicSet* pSet = pObject->members.values[i].asSet())
				{
					pSet->added.values.push_back(item);
				}
			}
			else
			{
				DYNAMIC_DATA_ERROR("Cant modify inherited set");
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

void DynamicData_remove_from_subobject_set(DynamicData* pValue, u64 hSetMember, DynamicData item)
{
	if (DynamicObject* pObject = pValue->asObject())
	{
		u64 i;
		if (findName(pObject->members.names, hSetMember, &i))
		{
			MemberStatus status = pObject->members.statuses[i];
			if (status == MemberStatus::Owned || status == MemberStatus::Instantiated)
			{
				if (DynamicSet* pSet = pObject->members.values[i].asSet())
				{
					if (findValue(pSet->added.values, &item, &i))
					{
						pSet->added.values.erase(pSet->added.values.begin() + (i64)i);
					}
					else
					{
						DYNAMIC_DATA_ERROR("object not in the local set");
					}
				}
				else
				{
					DYNAMIC_DATA_ERROR("member is not of type set");
				}
			}
			else
			{
				DYNAMIC_DATA_ERROR("member is not owned/instantiated");
			}
		}
		else
		{
			DYNAMIC_DATA_ERROR("didnt find key");
		}
	}
}

void DynamicData_remove_from_prototype_subobject_set(DynamicData* pValue, u64 hSetMember, DynamicData item)
{
	if (DynamicObject* pObject = pValue->asObject())
	{
		u64 i;
		if (findName(pObject->members.names, hSetMember, &i))
		{
			MemberStatus status = pObject->members.statuses[i];
			if (status == MemberStatus::Instantiated)
			{
				if (DynamicSet* pSet = pObject->members.values[i].asSet())
				{
					u64 unused;
					if (!findValue(pSet->removed.values, &item, &unused))
					{
						pSet->removed.values.push_back(item);
					}
					else
					{
						DYNAMIC_DATA_ERROR("object is already removed");
					}
				}
				else
				{
					DYNAMIC_DATA_ERROR("member is not of type set");
				}
			}
			else
			{
				DYNAMIC_DATA_ERROR("member is not instantiated");
			}
		}
		else
		{
			DYNAMIC_DATA_ERROR("didnt find key");
		}
	}
}

void DynamicData_cancel_remove_from_prototype_subobject_set(DynamicData* pValue, u64 hSetMember, DynamicData item)
{
	if (DynamicObject* pObject = pValue->asObject())
	{
		u64 i;
		if (findName(pObject->members.names, hSetMember, &i))
		{
			MemberStatus status = pObject->members.statuses[i];
			if (status == MemberStatus::Instantiated)
			{
				if (DynamicSet* pSet = pObject->members.values[i].asSet())
				{
					if (findValue(pSet->removed.values, &item, &i))
					{
						pSet->removed.values.erase(pSet->removed.values.begin() + (i64)i);
					}
					else
					{
						DYNAMIC_DATA_ERROR("object is not removed");
					}
				}
				else
				{
					DYNAMIC_DATA_ERROR("member is not of type set");
				}
			}
			else
			{
				DYNAMIC_DATA_ERROR("member is not instantiated");
			}
		}
		else
		{
			DYNAMIC_DATA_ERROR("didnt find key");
		}
	}
}

void DynamicData_set_compose(DynamicObject* pObject, u64 setMemberIndex, eastl::vector<u64>& ids, eastl::vector<DynamicData>& values)
{
	MemberStatus setStatus = pObject->members.statuses[setMemberIndex];

	if (pObject->members.values[setMemberIndex].type != DynamicData::Type_Set)
	{
		DYNAMIC_DATA_ERROR("Member is not a set");
		return;
	}

	if (setStatus == MemberStatus::Inherited)
	{
		DynamicObject* pPrototype = pObject->prototype.id() != 0 ? lookup_obj(pObject->prototype.id()) : nullptr;
		DynamicData_set_compose(pPrototype, setMemberIndex, ids, values);
	}
	else if (setStatus == MemberStatus::Instantiated)
	{
		DynamicObject* pPrototype = pObject->prototype.id() != 0 ? lookup_obj(pObject->prototype.id()) : nullptr;
		DynamicData_set_compose(pPrototype, setMemberIndex, ids, values);
		DynamicSet* pSet = pObject->members.values[setMemberIndex].asSet();

		for (DynamicData& add : pSet->added.values)
		{
			ids.push_back(add.id());
			values.push_back(add);
		}

		for (DynamicData& remove : pSet->removed.values)
		{
			u64 index;
			findId(ids, remove.id(), &index);

			ids.erase(ids.begin() + (i64)index);
			values.erase(values.begin() + (i64)index);
		}

		for (u64 i = 0; i < pSet->instantiated.ids.size(); ++i)
		{
			u64 id = pSet->instantiated.ids[i];
			DynamicData val = pSet->instantiated.values[i];

			u64 index;
			findId(ids, id, &index);
			ids[index] = val.id();
			values[index] = val;
		}
	}
	else if (setStatus == MemberStatus::Owned)
	{
		DynamicSet* pSet = pObject->members.values[setMemberIndex].asSet();

		for (DynamicData& add : pSet->added.values)
		{
			ids.push_back(add.id());
			values.push_back(add);
		}
	}
	else
	{
		DYNAMIC_DATA_ERROR("Type Error");
	}
}

eastl::vector<DynamicData> DynamicData_get_subobject_set(DynamicData* pValue, u64 hSetMember)
{
	eastl::vector<u64> ids;
	eastl::vector<DynamicData> values;

	if (DynamicObject* pObject = pValue->asObject())
	{
		u64 index;
		findName(pObject->members.names, hSetMember, &index);
		DynamicData_set_compose(pObject, index, ids, values);
	}
	else
	{
		DYNAMIC_DATA_ERROR("value is not an object");
	}

	return values;
}

eastl::vector<DynamicData> DynamicData_get_subobject_set_locally_removed(DynamicData* pValue, u64 hSetName)
{
	eastl::vector<DynamicData> setMembers;

	if (DynamicObject* pObject = pValue->asObject())
	{
		u64 i;
		if (findName(pObject->members.names, hSetName, &i))
		{
			if (DynamicSet* pSet = pObject->members.values[i].asSet())
			{
				setMembers = pSet->removed.values;
			}
		}
	}

	return setMembers;
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
	//	else if (findValue(pCurrent->members.values, &currentValue, &i))
	//	{
	//		// not instantiated, but exists in the members state. Instantiate it.
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

	u64 i;
	if (findValue(pCurrent->members.values, &currentValue, &i))
	{
		MemberStatus status = pCurrent->members.statuses[i];
		if (status == MemberStatus::Inherited)
		{
			DynamicData* pPrototype = &pCurrent->members.values[i];
			DynamicData created = DynamicData_new_from_prototype(pPrototype);

			pCurrent->members.values[i] = created;
			pCurrent->members.statuses[i] = MemberStatus::Instantiated;
			status = MemberStatus::Instantiated;
		}
		if (status == MemberStatus::Instantiated || status == MemberStatus::Owned)
		{
			DynamicData next = pCurrent->members.values[i];
			if (next.type == DynamicData::Type_Object)
			{
				DynamicData_instantiate_path_impl(pPath, next.asObject(), depth + 1, targetValue);
			}
			if (next.type == DynamicData::Type_Set)
			{
				DynamicData_instantiate_path_impl(pPath, next.asSet(), depth + 1, targetValue);
			}
		}
	}
}

void DynamicData_instantiate_path(DynamicEditorPath* pPath, DynamicData value)
{
	DynamicObject* pCurrent = pPath->root;
	DynamicData_instantiate_path_impl(pPath, pCurrent, 0, value);
}

const char* to_string(MemberStatus status)
{
	switch (status) {
	case MemberStatus::Owned:
		return "Owned";
	case MemberStatus::Inherited:
		return "Inherited";
	case MemberStatus::Instantiated:
		return "Instantiated";
	case MemberStatus::Overridden:
		return "Overridden";
	case MemberStatus::None:
		return "None";
	}
	assert(false && "enum out of range");

	return "enum : error";
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

void DynamicData_clone_internal(DynamicData* srcObject, DynamicData* dstObject)
{
	DynamicObject* pSrcObj = srcObject->asObject();
	DynamicObject* pDstObj = dstObject->asObject();

	u64 size = pSrcObj->members.values.size();

	pDstObj->members.names.resize(size);
	pDstObj->members.values.resize(size);
	pDstObj->members.statuses.resize(size);

	for (u64 i = 0; i< size; ++i)
	{
		DynamicData* pMember = &pSrcObj->members.values[i];
		DynamicData* pClone =  &pDstObj->members.values[i];
		pDstObj->members.names[i] = pSrcObj->members.names[i];
		pDstObj->members.statuses[i] = MemberStatus::Owned;

		switch (pMember->type)
		{
		case DynamicData::Type_Null:
			break;
		case DynamicData::Type_Object:
		{
			*pClone = DynamicData_obj_new();
			DynamicData_clone_internal(pMember, pClone);
			break;
		}
		case DynamicData::Type_Set:
		{
			eastl::vector<DynamicData> setMembers = DynamicData_get_subobject_set(srcObject, pSrcObj->members.names[i]);
			*pClone = DynamicData_set_new();
			pClone->asSet()->added.values.resize(setMembers.size());
			for (u64 ii = 0; ii < setMembers.size(); ++ii)
			{
				DynamicData_clone_internal(&setMembers[ii], &pClone->asSet()->added.values[ii]);
			}
			break;
		}
		case DynamicData::Type_Integer:
		{
			*pClone = DynamicData_int_new();
			pClone->integer = pMember->integer;
			break;
		}
		case DynamicData::Type_Number:
		{

			*pClone = DynamicData_num_new();
			pClone->number = pMember->number;
			break;
		}
		case DynamicData::Type_String:
		{
			*pClone = DynamicData_str_new();
			u64 capacity = pMember->string ? (strlen(pMember->string) + 1) : 0;
			if (capacity)
			{
				pClone->string = (char*)malloc(capacity);
				memcpy(pClone->string, pMember->string, capacity);
			}
			break;
		}
		}
	}
}

DynamicData DynamicData_clone(DynamicData* src)
{
	if (src->type == DynamicData::Type_Object)
	{
		DynamicData dst = DynamicData_obj_new();
		DynamicData_clone_internal(src, &dst);
		return dst;
	}
	else
	{
		DYNAMIC_DATA_ERROR("Can only clone an object");
		return DynamicData_make_null();
	}
}

DynamicData DynamicData_get_prototype(DynamicData* pValue)
{
	if (DynamicObject* pObject = pValue->asObject())
	{
		return pObject->prototype;
	}
	return DynamicData_make_null();
}


DynamicData DynamicData_obj_get_impl(DynamicObject* pObject, u64 hName)
{
	u64 i;
	if (findName(pObject->members.names, hName, &i))
	{
		if (pObject->members.statuses[i] == MemberStatus::Inherited)
		{
			return DynamicData_obj_get_impl(lookup_obj(pObject->prototype.hObject), hName);
		}
		else
		{
			return pObject->members.values[i];
		}
	}

	return DynamicData_make_null();
}

DynamicData DynamicData_obj_get(DynamicData* pValue, u64 hName)
{
	if (pValue->type == DynamicData::Type_Object)
	{
		DynamicObject* pObject = pValue->asObject();
		return DynamicData_obj_get_impl(pObject, hName);
	}

	return DynamicData_make_null();
}

MemberStatus DynamicData_get_member_status(DynamicData* pValue, u64 hMember)
{
	if (pValue->type == DynamicData::Type_Object)
	{
		DynamicObject* pObject = pValue->asObject();
		u64 i = 0;
		if (findName(pObject->members.names, hMember, &i))
		{
			return pObject->members.statuses[i];
		}
	}

	return MemberStatus::None;
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

		for (i32 i = 0; i < pObject->members.names.size(); ++i)
		{
			if (hName == pObject->members.names[i])
			{
				// todo: api add existing is error?
				pObject->members.values[i] = add;
				return;
			}
		}

		pObject->members.names.push_back(hName);
		pObject->members.values.push_back(add);
		pObject->members.statuses.push_back(MemberStatus::Owned);

		DynamicData_assign_root(target, &add);
	}
}

void DynamicData_obj_set(DynamicData* object, u64 hName, DynamicData value)
{
	if (value.type == DynamicData::Type_Object || value.type == DynamicData::Type_Set)
	{
		DYNAMIC_DATA_ERROR("assigning a set or object");
		return;
	}

	if (DynamicObject* pObject = object->asObject())
	{
		u64 i;
		if (findName(pObject->members.names, hName, &i))
		{
			MemberStatus status = pObject->members.statuses[i];
			if (status == MemberStatus::Inherited)
			{
				pObject->members.values[i] = value;
				pObject->members.statuses[i] = MemberStatus::Overridden;
			}
			else if (status == MemberStatus::Owned || status == MemberStatus::Overridden)
			{
				pObject->members.values[i] = value;
			}
		}
		else
		{
			DYNAMIC_DATA_ERROR(Printf("Object does not contain memeber %s", string_repository_get(hName)));
		}
	}
}

void DynamicData_obj_clear_override(DynamicData* pValue, u64 hMember)
{
	if (DynamicObject* pObject = pValue->asObject())
	{
		u64 i;
		if (findName(pObject->members.names, hMember, &i))
		{
			MemberStatus status = pObject->members.statuses[i];
			if (status == MemberStatus::Overridden)
			{
				pObject->members.statuses[i] = MemberStatus::Inherited;
			}
		}
		else
		{
			DYNAMIC_DATA_ERROR(Printf("Object does not contain memeber %s", string_repository_get(hMember)));
		}
	}
}

bool DynamicSet_is_up_to_date(DynamicObject* object, u64 hSetMember)
{
	/*{
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
	*/
	return false;
}

u64 string_repository_hash(const char* str)
{
	u64 key = murmur_hash_string(str);
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

void DynamicData_registerParser(u64 hType, DynamicDataParser_i parser)
{
	g_parsers[hType] = parser;
}

DDEntity DynamicData_readEntity(DynamicData* pEntity)
{
	DDEntity entity{};

	/*DynamicDataParser_i* parser = lookup_parser(hType);

	if (parser)
	{
		parser->parse(pEntity, &entity);
	}*/

	return entity;
}

DDTransformComponent DynamicData_readTransform(DynamicData* pEntity)
{
	DDTransformComponent component{};

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
	/*u64 hTypeId = MetroHash64::HashStr(s_typeIdKey);

	DynamicData type = DynamicData_obj_get(pData, hTypeId);

	DynamicDataParser_i* parser = lookup_parser(type.asUint());

	if (parser)
	{
		parser->write_back(pData, pValue);
	}*/
}

//void registerEntityTemplate()
//{
//	DynamicData root = DynamicData_obj_new();
//
//	u64 hTypeName = string_repository_hash(s_typeNameKey);
//	u64 hFields = string_repository_hash(s_fieldsKey);
//
//	DynamicData entityName = DynamicData_make_str("entity");
//	DynamicData_obj_add(&root, hTypeName, entityName);
//
//
//	DynamicData field_children = DynamicData_obj_new();
//	DynamicData_obj_add(&field_children, string_repository_hash("name"), DynamicData_make_str("Unnamed Entity"));
//	DynamicData_obj_add(&field_children, string_repository_hash("children"), DynamicData_set_new());
//	DynamicData_obj_add(&field_children, string_repository_hash("components"), DynamicData_set_new());
//
//	DynamicData entity_subobject = DynamicData_obj_new();
//	DynamicData_obj_add(&entity_subobject, string_repository_hash("sub-float"), DynamicData_num_new());
//	DynamicData_obj_add(&entity_subobject, string_repository_hash("sub-int"), DynamicData_num_new());
//
//	DynamicData nested_subobject = DynamicData_obj_new();
//	DynamicData_obj_add(&nested_subobject, string_repository_hash("nested-float"), DynamicData_num_new());
//	DynamicData_obj_add(&nested_subobject, string_repository_hash("nested-int"), DynamicData_num_new());
//	DynamicData_obj_add(&entity_subobject, string_repository_hash("nested-subobject"), nested_subobject);
//
//	DynamicData_obj_add(&field_children, string_repository_hash("subobject"), entity_subobject);
//
//	DynamicData_obj_add(&root, hFields, field_children);
//
//	DynamicDataParser_i parser;
//
//	parser.parse = [](DynamicData* value, void* target)
//	{
//		DDEntity* pEntity = (DDEntity*)target;
//
//		u64 hNameField = string_repository_hash("name");
//		u64 hChildrenField = string_repository_hash("children");
//		u64 hComponentsField = string_repository_hash("components");
//		
//		pEntity->name = DynamicData_obj_find(value, hNameField).asString();
//
//		DynamicSet* pChildren = DynamicData_obj_find(value, hChildrenField).asSet();
//
//		pEntity->children = pChildren->members.values;
//
//		DynamicSet* pComponents = DynamicData_obj_find(value, hComponentsField).asSet();
//		pEntity->components = pComponents->members.values;
//	};
//
//	parser.write_back = [](DynamicData* value, void* source)
//	{
//		DDEntity* pEntity = (DDEntity*)source;
//
//		if ((pEntity->editedMask & DDEntity::FieldMask_Name) != 0)
//		{
//			u64 hNameField = MetroHash64::HashStr("name");
//			DynamicData_obj_set(value, hNameField, DynamicData_make_str(pEntity->name));
//		}
//
//		if ((pEntity->editedMask & DDEntity::FieldMask_Children) != 0)
//		{
//			//u64 hChildrenField = MetroHash64::HashStr("children");
//			//DynamicData setChildren = DynamicData_set_new();
//			////arrChildren.pArray->values = pEntity->children;
//			//DynamicData_obj_set(value, hChildrenField, setChildren);
//		}
//
//		if ((pEntity->editedMask & DDEntity::FieldMask_Components) != 0)
//		{
//			//u64 hComponentsField = MetroHash64::HashStr("components");
//			//DynamicData arrComponents = DynamicData_set_new();
//			////arrComponents.pArray->values = pEntity->components;
//			//DynamicData_obj_set(value, hComponentsField, arrComponents);
//		}
//	};
//
//	DynamicData_registerParser(ENTITY_TYPE_ID, parser);
//
//	g_templates[ENTITY_TYPE_ID] = root;
//}

//void registerComponent_TransformTemplate()
//{
//	DynamicData root = DynamicData_obj_new();
//
//	u64 hTypeName = string_repository_hash(s_typeNameKey);
//	u64 hFields = string_repository_hash(s_fieldsKey);
//
//	DynamicData componentName = DynamicData_make_str("component_transform");
//	DynamicData_obj_add(&root, hTypeName, componentName);
//
//	DynamicData field_children = DynamicData_obj_new();
//
//	DynamicData_obj_add(&field_children, string_repository_hash("x"), DynamicData_num_new());
//	DynamicData_obj_add(&field_children, string_repository_hash("y"), DynamicData_num_new());
//	DynamicData_obj_add(&field_children, string_repository_hash("z"), DynamicData_num_new());
//
//	DynamicData_obj_add(&root, hFields, field_children);
//
//	DynamicDataParser_i parser;
//
//	parser.parse = [](DynamicData* value, void* data)
//	{
//		DDTransformComponent* pComponent = (DDTransformComponent*)data;
//
//		u64 hXField = MetroHash64::HashStr("x");
//		u64 hYField = MetroHash64::HashStr("y");
//		u64 hZField = MetroHash64::HashStr("z");
//
//		pComponent->x = (f32)DynamicData_obj_find(value, hXField).asNumber();
//		pComponent->y = (f32)DynamicData_obj_find(value, hYField).asNumber();
//		pComponent->z = (f32)DynamicData_obj_find(value, hZField).asNumber();
//	};
//
//	parser.write_back = [](DynamicData* value, void* data)
//	{
//		DDTransformComponent* pComponent = (DDTransformComponent*)data;
//
//		if ((pComponent->editedMask & DDTransformComponent::FieldMask_X) != 0)
//		{
//			u64 hXField = MetroHash64::HashStr("x");
//			DynamicData_obj_set(value, hXField, DynamicData_make_num(pComponent->x));
//		}
//
//		if ((pComponent->editedMask & DDTransformComponent::FieldMask_Y) != 0)
//		{
//			u64 hYField = MetroHash64::HashStr("y");
//			DynamicData_obj_set(value, hYField, DynamicData_make_num(pComponent->y));
//		}
//
//		if ((pComponent->editedMask & DDTransformComponent::FieldMask_Z) != 0)
//		{
//			u64 hZField = MetroHash64::HashStr("z");
//			DynamicData_obj_set(value, hZField, DynamicData_make_num(pComponent->z));
//		}
//	};
//
//	DynamicData_registerParser(COMPONENT_ID_TRANSFORM, parser);
//	g_templates[COMPONENT_ID_TRANSFORM] = root;
//}


DynamicData* DynamicData_get_template(u64 id)
{
	auto find = g_templates.find(id);

	if (find != g_templates.end())
	{
		return &find->second;
	}

	return nullptr;
}

eastl::vector<u64> DynamicData_get_all_types()
{
	eastl::vector<u64> res;
	for (eastl::pair<const u64, DynamicData>& kvp : g_templates)
	{
		res.push_back(kvp.first);
	}
	return res;
}

u64 DynamicData_register_type(const char* name, const DynamicDataPropertyDef* properties, u32 num_properties)
{
	DynamicData root = DynamicData_obj_new();

	u64 hTypeName = string_repository_hash(s_typeNameKey);
	u64 hTypeId = string_repository_hash(name);

	DynamicData typeName = DynamicData_make_str(name);
	DynamicData_obj_add(&root, hTypeName, typeName);

	DynamicData field_children = DynamicData_obj_new();
	for (u32 i = 0; i < num_properties; ++i)
	{
		DynamicData value = DynamicData_make_null();
		const DynamicDataPropertyDef* def = &properties[i];

		switch (properties[i].type)
		{
		case DynamicData::Type_Null:
		{
			value = DynamicData_make_null();
			break;
		}
		case DynamicData::Type_Object:
		{
			if (def->typeHash != 0)
			{
				value = DynamicData_create_from_template(def->typeHash);
			}
			else
			{
				value = DynamicData_obj_new();
			}
			break;
		}
		case DynamicData::Type_Set:
		{
			value = DynamicData_set_new();
			break;
		}
		case DynamicData::Type_Integer:
		{
			value = DynamicData_int_new();
			break;
		}
		case DynamicData::Type_Number:
		{
			value = DynamicData_num_new();
			break;
		}
		case DynamicData::Type_String:
		{
			value = DynamicData_str_new();
			break;
		}
		}

		if (value.type == DynamicData::Type_Null)
		{
			DYNAMIC_DATA_ERROR("Null field in defintion");
		}

		DynamicData_obj_add(&field_children, string_repository_hash(def->name), value);
	}

	DynamicData_obj_add(&root, s_hFields, field_children);

	g_templates[hTypeId] = root;

	return hTypeId;
}

DynamicData DynamicData_create_from_template(u64 hName)
{
	DynamicData* pTemplate = DynamicData_get_template(hName);

	if (pTemplate == nullptr)
	{
		DYNAMIC_DATA_ERROR("Could not find type");
		return DynamicData_make_null();
	}

	DynamicData fields = DynamicData_obj_get(pTemplate, s_hFields);
	DynamicData fieldsClone = DynamicData_clone(&fields);

	return fieldsClone;
}

void DynamicData_view(DynamicData* pData)
{
	DynamicEditorPath path;
	path.root = pData->asObject();

	ImGui::PushID((int)pData->hObject);

	if (ImGui::TreeNodeEx("Root"))
	{
		DynamicData_view_draw_root_object(pData);
		ImGui::TreePop();
	}

	ImGui::PopID();

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
