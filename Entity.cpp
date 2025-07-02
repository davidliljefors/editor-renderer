#include "Entity.h"

#include <stdio.h>

#include "Editor.h"
#include "imgui.h"

static i32 s_nextId = 0;


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
		buf.write("Object[%d]", DynamicData_size(&value));
		break;
	case DynamicData::Type_Array:
		buf.write("Array[%d]", DynamicData_size(&value));
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

void DynamicData_view_impl(DynamicData* pData)
{
	switch (pData->type) {
	case DynamicData::Type_Object:
	{
		DDObject* pObject = pData->asObject();
		DynamicData_obj_before_read(pObject);

		if (ImGui::TreeNodeEx(Printf("Object ID : %llu", pData->hObject), ImGuiTreeNodeFlags_Leaf))
		{
			ImGui::TreePop();
		}
		if (pObject->hPrototype)
		{
			if (ImGui::TreeNodeEx(Printf("Prototype ID : %llu", pObject->hPrototype), ImGuiTreeNodeFlags_Leaf))
			{
				ImGui::TreePop();
			}
		}

		for (u64 i = 0; i < pObject->flattened.names.size(); ++i)
		{

			const char* name = string_repository_get(pObject->flattened.names[i]);

			if (DynamicData_size(&pObject->flattened.values[i]) == 0)
			{
				Printf buf;
				DynamicData element = pObject->flattened.values[i];
				DynamicData_format_value(buf, pObject->flattened.values[i]);
				if (ImGui::TreeNodeEx(Printf("%s : %s", name, buf.cstr()), ImGuiTreeNodeFlags_Leaf))
				{
					ImGui::TreePop();
				}

				if (element.type == DynamicData::Type_Number && ImGui::IsItemClicked())
				{
					DynamicData newPos = DynamicData_make_num(element.asNumber() + 1);
					DynamicData_obj_set(pData, pObject->flattened.names[i], newPos);
				}
			}
			else 
			{
				if (ImGui::TreeNode(name))
				{
					DynamicData_view_impl(&pObject->flattened.values[i]);
					ImGui::TreePop();
				}
			}
		}
		break;
	}
	case DynamicData::Type_Array:
	{
		DDArray* pArray = pData->pArray;
		u64 size = DynamicData_size(pData);
		for (u64 i = 0; i < size; ++i)
		{
			if (ImGui::TreeNode(Printf("Element %d", i)))
			{
				DynamicData_view_impl(&pArray->values[i]);
				ImGui::TreePop();
			}
		}
		break;
	}
	case DynamicData::Type_Instance:
	{


		break;
	}
	case DynamicData::Type_Integer:
	case DynamicData::Type_Number:
	case DynamicData::Type_String:
	case DynamicData::Type_Null:
	{
		Printf buf;
		DynamicData_format_value(buf, *pData);
		bool r = ImGui::TreeNodeEx(buf.cstr(), ImGuiTreeNodeFlags_Leaf);
		if (r)
		{
			ImGui::TreePop();
		}
		break;
	}
	}
}

struct DDObjectOrPrototype
{
	
};

static eastl::unordered_map<u64, const char*> s_string_repository;
static std::unordered_map<u64, DynamicData> g_templates;
static std::unordered_map<u64, DynamicDataParser_i> g_parsers;
static std::unordered_map<u64, DDObject*> g_objects;
static std::unordered_map<u64, DDEdits*> g_edits;
static std::unordered_map<u64, DynamicData> g_prototypes;

DynamicDataParser_i* lookup_parser(u64 typeId)
{
	auto find = g_parsers.find(typeId);
	return find != g_parsers.end() ? &find->second : nullptr;
}

DDObject* lookup_obj(u64 hObject)
{
	auto find = g_objects.find(hObject);
	return find != g_objects.end() ? find->second : nullptr;
}

DDEdits* lookup_edits(u64 hEdits)
{
	auto find = g_edits.find(hEdits);
	return find != g_edits.end() ? find->second : nullptr;
}

bool has_field(DDObject* pObject, u64 hName)
{
	for (i32 i = 0; i < pObject->owned.names.size(); ++i)
	{
		if (hName == pObject->owned.names[i])
		{
			return true;
		}
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


bool DynamicEdits_find(DDObject::Edits* edits, u64 hName, u64* outIndex)
{
	for (u64 i = 0; i < edits->names.size(); ++i)
	{
		if (edits->names[i] == hName)
		{
			*outIndex = i;
			return true;
		}
	}
	return false;
}

PropertyRelation DynamicData_get_relation(DynamicData* pValue, u64 hName)
{
	return PropertyRelation::None;
}

DynamicData DynamicData_obj_new()
{
	DynamicData value;

	DDObject* pObject = new DDObject();

	u64 id = random_u64();

	value.type = DynamicData::Type_Object;
	value.hObject = id;

	pObject->version = 1;

	g_objects[id] = pObject;

	return value;
}

DynamicData DynamicData_instance_new(DynamicData* prototype)
{
	DynamicData value;

	void* mem = malloc(sizeof(DDObject));
	memset(mem, 0, sizeof(DDObject));

	u64 id = random_u64();
	DDObject* pObject = (DDObject*)mem;
	value.type = DynamicData::Type_Object;
	value.hObject = id;
	pObject->hPrototype = prototype->id();
	pObject->flattened.basedOnVersion = 0;
	pObject->version = 1;

	g_objects[id] = (DDObject*)mem;

	return value;
}

DynamicData DynamicData_obj_from_type(u64 typeId)
{
	DynamicData value;

	void* mem = malloc(sizeof(DDObject));
	memset(mem, 0, sizeof(DDObject));

	u64 id = random_u64();

	DDObject* pObject = (DDObject*)mem;
	g_objects[id] = pObject;

	value.type = DynamicData::Type_Object;
	value.hObject = id;


	return value;
}

DynamicData DynamicData_array_new()
{
	DynamicData value;

	void* mem = malloc(sizeof(DDArray));
	memset(mem, 0, sizeof(DDArray));

	value.type = DynamicData::Type_Array;
	value.pArray = (DDArray*)mem;

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

u64 DynamicData_size(const DynamicData* value)
{
	if (const DDObject* pObject = value->asObject())
	{
		return pObject->owned.values.size();
	}

	if (const DDArray* pArray = value->asArray())
	{
		return pArray->values.size();
	}

	return 0;
}

void DynamicData_clone_internal(const DynamicData* src, DynamicData* dst)
{
	switch (src->type)
	{
	case DynamicData::Type_Object:
	{
		*dst = DynamicData_obj_new();
		u64 size = DynamicData_size(src);
		DDObject* pObject = dst->asObject();
		const DDObject* pSrcObject = src->asObject();

		pObject->owned.names.resize(size);
		pObject->owned.values.resize(size);

		for (u64 i = 0; i < size; ++i)
		{
			pObject->owned.names[i] = pSrcObject->owned.names[i];
			DynamicData_clone_internal(&pSrcObject->owned.values[i], &pObject->owned.values[i]);
		}

		break;
	}
	case DynamicData::Type_Array:
	{
		*dst = DynamicData_array_new();
		u64 size = DynamicData_size(src);
		DDArray* pArray = dst->asArray();
		pArray->values.resize(size);
		const DDArray* srcArray = src->asArray();

		for (u64 i = 0; i < size; ++i)
		{
			DynamicData_clone_internal(&srcArray->values[i], &pArray->values[i]);
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

DynamicData DynamicData_clone(const DynamicData* src)
{
	DynamicData value;
	DynamicData_clone_internal(src, &value);
	return value;
}


// timplement ObjectEditor...
DynamicData DynamicData_obj_find(DynamicData* pValue, u64 hName)
{
	if (pValue->type == DynamicData::Type_Object)
	{
		DDObject* pObject = pValue->asObject();
		DynamicData_obj_before_read(pObject);
		u64 i;
		if (findName(pObject->flattened.names, hName, &i))
		{
			return pObject->flattened.values[i];
		}
	}

	return DynamicData_make_null();
}

void DynamicData_assign_root(DynamicData* newRoot, DynamicData* value)
{
	u64 rootId = newRoot->id();

	if (rootId)
	{
		if (DDObject* pObject = value->asObject())
		{
			pObject->hRoot = newRoot->id();
		}

		if (DDArray* pArray = value->asArray())
		{
			pArray->hRoot = rootId;
		}
	}
}

void DynamicData_obj_add(DynamicData* target, u64 hName, DynamicData add)
{
	if (DDObject* pObject = target->asObject())
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

	if (target->type == DynamicData::Type_Instance)
	{
		 
	}
}

void DynamicData_obj_set(DynamicData* target, u64 hName, DynamicData value)
{
	if (DDObject* pObject = target->asObject())
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
			DynamicEdit edit;
			edit.type = DynamicEdit::Type_ObjectSet;
			edit.objectSet.value = value;

			u64 i;
			if (findName(pObject->edits.names, hName, &i))
			{
				// todo remove the previous edits if necessary

				// Its currently an edit, re-assign the edit
				pObject->edits.edits[i].push_back(edit);
			}
			else
			{
				pObject->edits.names.push_back(hName);
				pObject->edits.edits.push_back({});
				pObject->edits.edits.back().push_back(edit);
			}

			if (findName(pObject->flattened.names, hName, &i))
			{
				pObject->flattened.values[i] = value;
			}
			else
			{
				pObject->flattened.names.push_back(hName);
				pObject->flattened.values.push_back(value);
			}

			pObject->version++;
			return;
		}
	}

	assert(false && "didnt find key");
}

void DynamicData_obj_arr_push(DynamicData* object, u64 hArrayName, DynamicData value)
{
	if (DDObject* pObject = object->asObject())
	{
		DynamicData_obj_before_read(pObject);

		if (pObject->hPrototype == 0)
		{
			for (i32 i = 0; i < pObject->owned.names.size(); ++i)
			{
				if (hArrayName == pObject->owned.names[i])
				{
					pObject->owned.values[i].asArray()->values.push_back(value);
					pObject->version++;
					return;
				}
			}

			assert(false && "didnt find key");
		}
		else
		{
			DynamicEdit edit;
			edit.type = DynamicEdit::Type_ArrayAppend;
			edit.arrayAppend.value = value;

			u64 i;
			if (findName(pObject->edits.names, hArrayName, &i))
			{
				pObject->edits.edits[i].push_back(edit);
			}
			else
			{
				pObject->edits.names.push_back(hArrayName);
				pObject->edits.edits.push_back({});
				pObject->edits.edits.back().push_back(edit);
			}

			if (findName(pObject->flattened.names, hArrayName, &i))
			{
				pObject->flattened.values[i] = value;
			}
			else
			{
				pObject->flattened.names.push_back(hArrayName);
				pObject->flattened.values.push_back(value);
			}
			pObject->version++;
			return;
		}
	}
	assert(false && "didnt find key");
}

void DynamicData_instantiate_impl(DynamicData* value, DynamicData* target)
{
	
}

DynamicData DynamicData_new_from_prototype(DynamicData* pPrototype)
{
	DynamicData instance = DynamicData_instance_new(pPrototype);
	return instance;
}

DynamicData DynamicData_instantiate_member(DynamicData* pValue, u64 hName)
{
	DDObject* pObject = pValue->asObject();
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
			return instance;
		}
	}

	return DynamicData_make_null();
}

void DynamicData_array_add(DynamicData* array, DynamicData value)
{
	if (array->type == DynamicData::Type_Array)
	{
		DDArray* pArray = array->pArray;
		pArray->values.push_back(value);
	}
}

void DynamicData_array_pop(DynamicData* array, DynamicData value)
{

}

void DynamicData_array_compose_internal(DDObject* object, u64 hName, eastl::vector<DynamicData>& composed)
{
	if (object->hPrototype != 0)
	{
		DynamicData_array_compose_internal(lookup_obj(object->hPrototype), hName, composed);
	}
	else
	{
		u64 index;

		if (DynamicEdits_find(&object->edits, hName, &index))
		{
			eastl::vector<DynamicEdit>& dynamicEdits = object->edits.edits[index];

			if (dynamicEdits.size() == 1 && dynamicEdits[0].type == DynamicEdit::Type_ObjectAdd)
			{
				DDArray* asArray = dynamicEdits[0].objectAdd.value.asArray();
				if (asArray)
				{
					composed = asArray->values;
				}
				return;
			}

			for (DynamicEdit& dynamicEdit : dynamicEdits)
			{
				if (dynamicEdit.type == DynamicEdit::Type_ArrayAppend)
				{
					composed.push_back(dynamicEdit.arrayAppend.value);
				}
				if (dynamicEdit.type == DynamicEdit::Type_ArrayPop)
				{
					composed.pop_back();
				}
			}
		}
	}
}

void DynamicData_apply_obj_edit(eastl::vector<u64>& names, eastl::vector<DynamicData>& values, u64 hName, DynamicEdit edit)
{
	switch (edit.type)
	{
	case DynamicEdit::Type_ArrayAppend:
		u64 i;
		if (findName(names, hName, &i))
		{
			values[i].asArray()->values.push_back(edit.arrayAppend.value);
		}
		break;
	case DynamicEdit::Type_ArrayPop:
	case DynamicEdit::Type_ArraySet:
		assert(false && " not implemented ");
		break;
	case DynamicEdit::Type_ObjectAdd:
		values.push_back(edit.objectAdd.value);
		break;
	case DynamicEdit::Type_ObjectRemove:
	{
		u64 i;
		if (findName(names, hName, &i))
		{
			names.erase(names.begin() + i);
			values.erase(values.begin() + i);
		}
		break;
	}
	case DynamicEdit::Type_ObjectSet:
	{
		u64 i;
		if (findName(names, hName, &i))
		{
			values[i] = edit.objectSet.value;
		}
	}
	break;
	}
}

void DynamicData_obj_compose(DDObject* pObject, eastl::vector<u64>& names, eastl::vector<DynamicData>& values)
{
	DDObject* pPrototype = pObject->hPrototype != 0 ? lookup_obj(pObject->hPrototype) : nullptr;

	// evaluate prototype
	if (!pPrototype)
	{
		// todo remove
		pObject->flattened.names = pObject->owned.names;
		pObject->flattened.values = pObject->owned.values;

		names = pObject->owned.names;
		values = pObject->owned.values;
	}
	if (pPrototype)
	{
		if (pObject->flattened.basedOnVersion == pPrototype->version)
		{
			// todo if we have no edits at top level we can directly reference the arrays
			names = pObject->flattened.names;
			values = pObject->flattened.values;
			return;
		}

		DynamicData_obj_compose(pPrototype, names, values);
		pObject->flattened.basedOnVersion = pPrototype->version;
	}

	// apply edits
	for (u64 i = 0; i < pObject->edits.names.size(); ++i)
	{
		u64 hName = pObject->edits.names[i];
		eastl::vector<DynamicEdit>& edits = pObject->edits.edits[i];

		for (auto& edit : edits)
		{
			DynamicData_apply_obj_edit(names, values, hName, edit);
		}
	}

	// Redirect instances and assign flattened state
	for (u64 i = 0; i < pObject->instantiated.names.size(); ++i)
	{
		u64 hName = pObject->instantiated.names[i];
		u64 hObject = pObject->instantiated.values[i].hObject;

		u64 prototypeIndex;
		// for an instance we need to redirect the values to our instance. applying the instance edits.
		// todo if its no longer in prototype clean it up
		if (findName(names, hName, &prototypeIndex))
		{
			assert(values[prototypeIndex].type == DynamicData::Type_Object);
			assert(pObject->instantiated.values[i].type == DynamicData::Type_Object);

			if (values[prototypeIndex].type == DynamicData::Type_Object)
			{
				values[prototypeIndex].hObject = hObject;
			}
			DDObject* pInstantiated = lookup_obj(hObject);
			pInstantiated->flattened.values = values;
			pInstantiated->flattened.names = names;
		}
	}

	if (pObject->hPrototype != 0)
	{
		pObject->flattened.names = names;
		pObject->flattened.values = values;
	}
}

void DynamicData_obj_before_read(DDObject* pObject)
{
	eastl::vector<u64> names;
	eastl::vector<DynamicData> values;
	DynamicData_obj_compose(pObject, names, values);
}

eastl::vector<DynamicData> DynamicData_array_compose(DDObject* object, u64 hName)
{
	// todo cleanup allocations
	eastl::vector<DynamicData> composed;
	DynamicData_array_compose_internal(object, hName, composed);
	return composed;
}

void ArrayEditor::push(DynamicData value)
{
	if (isInstanced)
	{
		u64 size = instance.pEdits->names.size();
		u64 index = u64(-1);
		for (u64 i = 0; i < size; ++i)
		{
			if (instance.pEdits->names[i] == hName)
			{
				index = i;
				break;
			}
		}
		if (index == u64(-1))
		{
			index = size;
			instance.pEdits->names.push_back(hName);
			instance.pEdits->edits.push_back({});
		}
		instance.pEdits->edits[index].push_back(DynamicEdit_arrayAdd(value));
	}
	else
	{
		owned.pArray->values.push_back(value);
	}
}

void ArrayEditor::pop()
{
	if (isInstanced)
	{
		if (!instance.flatValues.empty())
		{
			u64 size = instance.pEdits->names.size();
			u64 index = u64(-1);
			for (u64 i = 0; i < size; ++i)
			{
				if (instance.pEdits->names[i] == hName)
				{
					index = i;
					break;
				}
			}
			if (index == u64(-1))
			{
				index = size;
				instance.pEdits->names.push_back(hName);
				instance.pEdits->edits.push_back({});
			}

			instance.flatValues.pop_back();
			instance.pEdits->edits[index].push_back(DynamicEdit_arrayPop());
		}
	}
	else
	{
		owned.pArray->values.pop_back();
	}
}

DynamicData ArrayEditor::get(u64 index)
{
	if (isInstanced)
	{
		return instance.flatValues[index];
	}
	else
	{
		return owned.pArray->values[index];
	}
}

u64 ArrayEditor::size()
{
	if (isInstanced)
	{
		return instance.flatValues.size();
	}
	else
	{
		return owned.pArray->values.size();
	}
}

ArrayEditor DynamicData_edit_array(DynamicData* object, u64 hName)
{
	DDObject* pObject = object->asObject();

	ArrayEditor editor{};
	

	if (pObject)
	{
		if (pObject->hPrototype)
		{
			editor.instance.flatValues = DynamicData_array_compose(pObject, hName);
			editor.instance.pEdits = &pObject->edits;
			editor.isInstanced = true;
		}
		else
		{
			u64 i;
			if (findName(pObject->owned.names, hName, &i))
			{
				editor.owned.pArray = pObject->owned.values[i].asArray();
			}
		}

	}

	return editor;
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

	if (DDArray* pComponents = DynamicData_obj_find(pEntity, hComponents).asArray())
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
	}

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
	DynamicData_obj_add(&field_children, string_repository_hash("children"), DynamicData_array_new());
	DynamicData_obj_add(&field_children, string_repository_hash("components"), DynamicData_array_new());
	DynamicData_obj_add(&field_children, string_repository_hash("test_number"), DynamicData_num_new());
	DynamicData_obj_add(&root, hFields, field_children);

	DynamicDataParser_i parser;

	parser.parse = [](DynamicData* value, void* target)
	{
		DDEntity* pEntity = (DDEntity*)target;

		u64 hNameField = MetroHash64::HashStr("name");
		u64 hChildrenField = MetroHash64::HashStr("children");
		u64 hComponentsField = MetroHash64::HashStr("components");

		pEntity->id = value->id();
		if (auto obj = value->asObject())
		{
			if (obj->hPrototype != 0)
			{
				//__debugbreak();
			}
		}
		DDObject* pObject = value->asObject();
		(void)pObject;
		pEntity->name = DynamicData_obj_find(value, hNameField).asString();
		pEntity->children = DynamicData_obj_find(value, hChildrenField).asArray()->values;
		pEntity->components = DynamicData_obj_find(value, hComponentsField).asArray()->values;
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
			u64 hChildrenField = MetroHash64::HashStr("children");
			DynamicData arrChildren = DynamicData_array_new();
			arrChildren.pArray->values = pEntity->children;
			DynamicData_obj_set(value, hChildrenField, arrChildren);
		}

		if ((pEntity->editedMask & DDEntity::FieldMask_Components) != 0)
		{
			u64 hComponentsField = MetroHash64::HashStr("components");
			DynamicData arrComponents = DynamicData_array_new();
			arrComponents.pArray->values = pEntity->components;
			DynamicData_obj_set(value, hComponentsField, arrComponents);
		}
	};

	DynamicData_registerParser(ENTITY_TYPE_ID, parser);

	g_templates[ENTITY_TYPE_ID] = root;
}

//void registerComponentTemplate()
//{
//	DynamicData root = DynamicData_obj_new();
//
//	u64 hTypeName = string_repository_hash(s_typeNameKey);
//	u64 hFields = string_repository_hash(s_fieldsKey);
//
//	DynamicData componentName = DynamicData_make_str("component");
//	DynamicData_obj_add(root, hTypeName, componentName);
//
//	DynamicData arrFields = DynamicData_array_new();
//
//	DynamicData field_children = DynamicData_obj_new();
//	DynamicData_obj_add(field_children, string_repository_hash("field_name"), DynamicData_make_str("fields"));
//	DynamicData_obj_add(field_children, string_repository_hash("field_type"), DynamicData_make_str("array"));
//	DynamicData_obj_add(field_children, string_repository_hash("field_element_type"), DynamicData_make_str("object"));
//	DynamicData_array_add(arrFields, field_children);
//
//	DynamicData_obj_add(root, hFields, arrFields);
//
//	g_templates[COMPONENT_TYPE_ID] = root;
//}

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

DynamicData* DynamicData_getTemplate(u64 id)
{
	return &g_templates[id];
}

DynamicData DynamicData_createFromTemplate(u64 hTemplate)
{
	DynamicData value = DynamicData_obj_new();

	DynamicData* pTemplate = DynamicData_getTemplate(hTemplate);
	
	u64 hType = string_repository_hash(s_typeNameKey);
	u64 hTypeId = string_repository_hash(s_typeIdKey);

	const char* typeName = DynamicData_obj_find(pTemplate, hType).asString();

	DynamicData_obj_add(&value, hType, DynamicData_make_str(typeName));
	DynamicData_obj_add(&value, hTypeId, DynamicData_make_int((i64)hTemplate));

	u64 hFields = string_repository_hash(s_fieldsKey);

	if (DDObject* fields = DynamicData_obj_find(pTemplate, hFields).asObject())
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
	DynamicData_view_impl(pData);
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
