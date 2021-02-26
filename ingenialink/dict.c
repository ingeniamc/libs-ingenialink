/*
 * MIT License
 *
 * Copyright (c) 2017 Ingenia-CAT S.L.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "dict.h"

#include <inttypes.h>

#include "ingenialink/err.h"
#include "ingenialink/utils.h"

/*******************************************************************************
 * Private
 ******************************************************************************/

/** Dummy libxml2 error function (so that no garbage is put to stderr/stdout) */
static void xml_error(void *ctx, const char *msg, ...)
{
	(void)ctx;
	(void)msg;
}

/**
 * Parse labels.
 *
 * @param [in] node
 *	XML Node.
 * @param [in, out] labels
 *	Labels dictionary.
 *
 * @return
 *	0 on success, error code otherwise.
 */
static int parse_labels(xmlNodePtr node, il_dict_labels_t *labels)
{
	xmlNode *label;

	for (label = node->children; label; label = label->next) {
		xmlChar *lang, *content;

		if (label->type != XML_ELEMENT_NODE)
			continue;

		lang = xmlGetProp(label, (const xmlChar *)"lang");
		if (!lang) {
			ilerr__set("Malformed label entry");
			return IL_EFAIL;
		}

		content = xmlNodeGetContent(label);
		if (content) {
			il_dict_labels_set(labels,
					   (const char *)lang,
					   (const char *)content);
			xmlFree(content);
		}

		xmlFree(lang);
	}

	return 0;
}

/**
 * Parse sub-category.
 *
 * @param [in] node
 *	XML Node.
 * @param [in, out] h_scats
 *	Sub-categories hash table.
 *
 * @return
 *	0 on success, error code otherwise.
 */
static int parse_scat(xmlNodePtr node, khash_t(scat_id) * h_scats)
{
	int r, absent;
	khint_t k;
	il_dict_labels_t *labels;
	xmlChar *id;

	/* parse: id (required), insert to hash table */
	id = xmlGetProp(node, (const xmlChar *)"id");
	if (!id) {
		ilerr__set("Malformed sub-category entry (id missing)");
		return IL_EFAIL;
	}

	k = kh_put(scat_id, h_scats, (char *)id, &absent);
	if (!absent) {
		ilerr__set("Found duplicated sub-category: %s", id);
		r = IL_EFAIL;
		goto cleanup_id;
	}

	/* create labels dictionary */
	labels = il_dict_labels_create();
	if (!labels) {
		r = IL_EFAIL;
		goto cleanup_entry;
	}

	kh_val(h_scats, k) = labels;

	/* parse labels */
	for (node = node->children; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (xmlStrcmp(node->name, (const xmlChar *)"Labels") == 0) {
			r = parse_labels(node, labels);
			if (r < 0)
				goto cleanup_labels;
		}
	}

	return 0;

cleanup_labels:
	il_dict_labels_destroy(labels);
	kh_val(h_scats, k) = NULL;

cleanup_entry:
	kh_del(scat_id, h_scats, k);

cleanup_id:
	xmlFree(id);

	return r;
}

/**
 * Parse sub-categories.
 *
 * @param [in] node
 *	XML Node.
 * @param [in, out] h_scats
 *	Sub-categories hash table.
 *
 * @return
 *	0 on success, error code otherwise.
 */
static int parse_scats(xmlNodePtr node, khash_t(scat_id) * h_scats)
{
	int r;
	khint_t k;

	/* parse subcategories */
	for (node = node->children; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (xmlStrcmp(node->name,
			      (const xmlChar *)"Subcategory") == 0) {
			r = parse_scat(node, h_scats);
			if (r < 0)
				goto cleanup_h_scats;
		}
	}

	return 0;

cleanup_h_scats:
	/* delete successfully inserted subcategories */
	for (k = 0; k < kh_end(h_scats); ++k) {
		if (kh_exist(h_scats, k)) {
			il_dict_labels_destroy(kh_val(h_scats, k));
			xmlFree((char *)kh_key(h_scats, k));
		}
	}

	return r;
}

/**
 * Parse category node.
 *
 * @param [in] node
 *	XML Node.
 * @param [in, out] dict
 *	Dictionary instance.
 *
 * @return
 *	0 on success, error code otherwise.
 */
static int parse_cat(xmlNodePtr node, il_dict_t *dict)
{
	int r, absent;
	khint_t k;
	il_dict_labels_t *labels;
	xmlChar *id;

	khash_t(scat_id) * h_scats;

	/* parse: id (required), insert to hash table */
	id = xmlGetProp(node, (const xmlChar *)"id");
	if (!id) {
		ilerr__set("Malformed category entry (id missing)");
		return IL_EFAIL;
	}

	k = kh_put(cat_id, dict->h_cats, (char *)id, &absent);
	if (!absent) {
		ilerr__set("Found duplicated category: %s", id);
		xmlFree(id);
		return IL_EFAIL;
	}

	/* create labels and sub-categories dictionaries */
	labels = il_dict_labels_create();
	if (!labels)
		return IL_EFAIL;

	kh_val(dict->h_cats, k).labels = labels;

	h_scats = kh_init(scat_id);
	if (!h_scats) {
		ilerr__set("Sub-categories hash table allocation failed");
		goto cleanup_labels;
	}

	kh_val(dict->h_cats, k).h_scats = h_scats;

	/* parse labels and subcategories */
	for (node = node->children; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (xmlStrcmp(node->name, (const xmlChar *)"Labels") == 0) {
			r = parse_labels(node, labels);
			if (r < 0)
				goto cleanup_h_scats;
		}

		if (xmlStrcmp(node->name,
			      (const xmlChar *)"Subcategories") == 0) {
			r = parse_scats(node, h_scats);
			if (r < 0)
				goto cleanup_h_scats;
		}
	}

	return 0;

cleanup_h_scats:
	kh_destroy(scat_id, h_scats);
	kh_val(dict->h_cats, k).h_scats = NULL;

cleanup_labels:
	il_dict_labels_destroy(labels);
	kh_val(dict->h_cats, k).labels = NULL;

	return r;
}

/**
 * Obtain data type from dictionary name.
 *
 * @param [in] name
 *	Name.
 * @param [out] dtype
 *	Where data type will be stored.
 *
 * @return
 *	0 on success, IL_EINVAL if unknown.
 */
static int get_dtype(const char *name, il_reg_dtype_t *dtype)
{
	static const il_dict_dtype_map_t map[] = {
		{ "u8", IL_REG_DTYPE_U8 },
		{ "s8", IL_REG_DTYPE_S8 },
		{ "u16", IL_REG_DTYPE_U16 },
		{ "s16", IL_REG_DTYPE_S16 },
		{ "u32", IL_REG_DTYPE_U32 },
		{ "s32", IL_REG_DTYPE_S32 },
		{ "u64", IL_REG_DTYPE_U64 },
		{ "s64", IL_REG_DTYPE_S64 },
		{ "float", IL_REG_DTYPE_FLOAT },
		{ "str", IL_REG_DTYPE_STR },
	};

	size_t i;

	for (i = 0; i < ARRAY_SIZE(map); i++) {
		if (strcmp(map[i].name, name) == 0) {
			*dtype = map[i].dtype;
			return 0;
		}
	}

	ilerr__set("Data type not supported (%s)", name);
	return IL_EINVAL;
}

/**
 * Parse storage value.
 *
 * @param [in] value
 *	Storage value.
 * @param [out] reg
 *	Register.
 */
static void get_storage(const char *value, il_reg_t *reg)
{
	switch (reg->dtype) {
	case IL_REG_DTYPE_U8:
		reg->storage.u8 = (uint8_t)strtoul(value, NULL, 0);
		break;
	case IL_REG_DTYPE_S8:
		reg->storage.s8 = (int8_t)strtol(value, NULL, 0);
		break;
	case IL_REG_DTYPE_U16:
		reg->storage.u16 = (uint16_t)strtol(value, NULL, 0);
		break;
	case IL_REG_DTYPE_S16:
		reg->storage.s16 = (int16_t)strtoul(value, NULL, 0);
		break;
	case IL_REG_DTYPE_U32:
		reg->storage.u32 = (uint32_t)strtoul(value, NULL, 0);
		break;
	case IL_REG_DTYPE_S32:
		reg->storage.s32 = (int32_t)strtol(value, NULL, 0);
		break;
	case IL_REG_DTYPE_U64:
		reg->storage.u64 = (uint64_t)strtoull(value, NULL, 0);
		break;
	case IL_REG_DTYPE_S64:
		reg->storage.s64 = (int64_t)strtoll(value, NULL, 0);
		break;
	case IL_REG_DTYPE_FLOAT:
		reg->storage.flt = strtof(value, NULL);
		break;
	default:
		break;
	}
}

/**
 * Obtain access type from dictionary name.
 *
 * @param [in] name
 *	Name.
 * @param [out] access
 *	Where access type will be stored.
 *
 * @return
 *	0 on success, IL_EINVAL if unknown.
 */
static int get_access(const char *name, il_reg_access_t *access)
{
	static const il_dict_access_map_t map[] = {
		{ "r", IL_REG_ACCESS_RO },
		{ "w", IL_REG_ACCESS_WO },
		{ "rw", IL_REG_ACCESS_RW },
	};

	size_t i;

	for (i = 0; i < ARRAY_SIZE(map); i++) {
		if (strcmp(map[i].name, name) == 0) {
			*access = map[i].access;
			return 0;
		}
	}

	ilerr__set("Access type not supported (%s)", name);
	return IL_EINVAL;
}

/**
 * Obtain physical units type from dictionary name.
 *
 * @param [in] name
 *	Name.
 *
 * @return
 *	Physical units type (defaults to IL_REG_PHY_NONE if unknown).
 */
static il_reg_phy_t get_phy(const char *name)
{
	static const il_dict_phy_map_t map[] = {
		{ "none", IL_REG_PHY_NONE },
		{ "torque", IL_REG_PHY_TORQUE },
		{ "pos", IL_REG_PHY_POS },
		{ "vel", IL_REG_PHY_VEL },
		{ "acc", IL_REG_PHY_ACC },
		{ "volt_rel", IL_REG_PHY_VOLT_REL },
		{ "rad", IL_REG_PHY_RAD },
	};

	size_t i;

	for (i = 0; i < ARRAY_SIZE(map); i++) {
		if (strcmp(map[i].name, name) == 0)
			return map[i].phy;
	}

	return IL_REG_PHY_NONE;
}

/**
 * Parse register range.
 *
 * @param [in] node
 *	XML Node.
 * @param [in, out] reg
 *	Register.
 */
static void parse_reg_range(xmlNodePtr node, il_reg_t *reg)
{
	xmlChar *val;

	val = xmlGetProp(node, (const xmlChar *)"min");
	if (val) {
		switch (reg->dtype) {
		case IL_REG_DTYPE_U8:
			reg->range.min.u8 = (uint8_t)strtoul(
				(const char *)val, NULL, 0);
			break;
		case IL_REG_DTYPE_S8:
			reg->range.min.s8 = (int8_t)strtol(
				(const char *)val, NULL, 0);
			break;
		case IL_REG_DTYPE_U16:
			reg->range.min.u16 = (uint16_t)strtol(
				(const char *)val, NULL, 0);
			break;
		case IL_REG_DTYPE_S16:
			reg->range.min.s16 = (int16_t)strtoul(
				(const char *)val, NULL, 0);
			break;
		case IL_REG_DTYPE_U32:
			reg->range.min.u32 = (uint32_t)strtoul(
				(const char *)val, NULL, 0);
			break;
		case IL_REG_DTYPE_S32:
			reg->range.min.s32 = (int32_t)strtol(
				(const char *)val, NULL, 0);
			break;
		case IL_REG_DTYPE_U64:
			reg->range.min.u64 = (uint64_t)strtoull(
				(const char *)val, NULL, 0);
			break;
		case IL_REG_DTYPE_S64:
			reg->range.min.s64 = (int64_t)strtoll(
				(const char *)val, NULL, 0);
			break;
		case IL_REG_DTYPE_FLOAT:
			reg->range.min.flt = (float)strtof(
				(const char *)val, NULL, 0);
			break;
		default:
			break;
		}

		xmlFree(val);
	}

	val = xmlGetProp(node, (const xmlChar *)"max");
	if (val) {
		switch (reg->dtype) {
		case IL_REG_DTYPE_U8:
			reg->range.max.u8 = (uint8_t)strtoul(
				(const char *)val, NULL, 0);
			break;
		case IL_REG_DTYPE_S8:
			reg->range.max.s8 = (int8_t)strtol(
				(const char *)val, NULL, 0);
			break;
		case IL_REG_DTYPE_U16:
			reg->range.max.u16 = (uint16_t)strtol(
				(const char *)val, NULL, 0);
			break;
		case IL_REG_DTYPE_S16:
			reg->range.max.s16 = (int16_t)strtoul(
				(const char *)val, NULL, 0);
			break;
		case IL_REG_DTYPE_U32:
			reg->range.max.u32 = (uint32_t)strtoul(
				(const char *)val, NULL, 0);
			break;
		case IL_REG_DTYPE_S32:
			reg->range.max.s32 = (int32_t)strtol(
				(const char *)val, NULL, 0);
			break;
		case IL_REG_DTYPE_U64:
			reg->range.max.u64 = (uint64_t)strtoull(
				(const char *)val, NULL, 0);
			break;
		case IL_REG_DTYPE_S64:
			reg->range.max.s64 = (int64_t)strtoll(
				(const char *)val, NULL, 0);
			break;
		case IL_REG_DTYPE_FLOAT:
			reg->range.max.flt = (float)strtof(
				(const char *)val, NULL, 0);
			break;
		default:
			break;
		}

		xmlFree(val);
	}
}

/**
 * Parse register enumeration.
 *
 * @param [in] node
 *	XML Node.
 * @param [in, out] reg
 *	Register.
 */
static void parse_reg_enums(xmlNodePtr node, il_reg_t *reg)
{
	xmlNode *enumeration;
	int index = 0;
	for (enumeration = node->children; enumeration; enumeration = enumeration->next) {
		xmlChar *value, *content;

		if (enumeration->type != XML_ELEMENT_NODE)
			continue;

		content = xmlNodeGetContent(enumeration);
		if (content) {
			il_reg_enum_t new_enum;
			value = xmlGetProp(enumeration, (const xmlChar *)"value");
			if (value != NULL) {
				reg->enums[index].value = atoi(value);
				reg->enums[index].label = strdup(content);
				index = index + 1;
				reg->enums_count = reg->enums_count + 1;
			}
		}
	}
}


/**
 * Parse register properties.
 *
 * @param [in] node
 *	XML Node.
 * @param [in, out] reg
 *	Register.
 *
 * @return
 *	0 on success, error code otherwise.
 */
static int parse_reg_props(xmlNodePtr node, il_reg_t *reg)
{
	int r;
	xmlNode *prop;

	for (prop = node->children; prop; prop = prop->next) {
		if (prop->type != XML_ELEMENT_NODE)
			continue;

		if (xmlStrcmp(prop->name, (const xmlChar *)"Labels") == 0) {
			reg->labels = il_dict_labels_create();
			if (!reg->labels)
				return IL_EFAIL;

			r = parse_labels(prop, reg->labels);
			if (r < 0)
				goto cleanup_labels;
		}

		if (xmlStrcmp(prop->name, (const xmlChar *)"Range") == 0)
			parse_reg_range(prop, reg);
		reg->enums_count = 0;
		if (xmlStrcmp(prop->name, (const xmlChar *)"Enumerations") == 0) {

			parse_reg_enums(prop, reg);
		}


	}

	return 0;

cleanup_labels:
	il_dict_labels_destroy(reg->labels);

	return r;
}

/**
 * Parse register node.
 *
 * @param [in] node
 *	XML Node.
 * @param [in, out] dict
 *	Dictionary instance.
 *
 * @return
 *	0 on success, error code otherwise.
 */
static int parse_reg(xmlNodePtr node, il_dict_t *dict, int subnode)
{
	int r, absent;
	khint_t k;
	il_reg_t *reg;
	xmlChar *id, *param;

	/* parse: id (required), insert to hash table */
	id = xmlGetProp(node, (const xmlChar *)"id");
	if (!id) {
		ilerr__set("Malformed entry (id missing)");
		return IL_EFAIL;
	}

	/* parse: subnode */
	param = xmlGetProp(node, (const xmlChar *)"subnode");
	if (!param) {
		subnode = 1;
	}
	else {
		subnode = strtoul((char *)param, NULL, 4);
	}

	k = kh_put(reg_id, dict->h_regs[subnode], (char *)id, &absent);
	if (!absent) {
		ilerr__set("Found duplicated register: %s", id);
		xmlFree(id);
		return IL_EFAIL;
	}

	/* store XML node (for later editions) */
	kh_val(dict->h_regs[subnode], k).xml_node = node;

	/* initialize register */
	reg = &kh_val(dict->h_regs[subnode], k).reg;
	reg->labels = NULL;
	reg->cat_id = NULL;
	reg->identifier = (char *)id;
	reg->subnode = subnode;

	/* parse: units */
	xmlChar *units;
	units = xmlGetProp(node, (const xmlChar *)"units");
	reg->units = (char *)units;

	/* parse: cyclic */
	xmlChar *cyclic;
	cyclic = xmlGetProp(node, (const xmlChar *)"cyclic");
	if (!cyclic) reg->cyclic = "";
	else reg->cyclic = (char *)cyclic;

	/* parse: address */
	param = xmlGetProp(node, (const xmlChar *)"address");
	if (!param) {
		ilerr__set("Malformed entry (%s, missing address)", id);
		return IL_EFAIL;
	}

	reg->address = strtoul((char *)param, NULL, 16);

	xmlFree(param);

	/* parse: dtype */
	param = xmlGetProp(node, (const xmlChar *)"dtype");
	if (!param) {
		ilerr__set("Malformed entry (%s, missing dtype)", id);
		return IL_EFAIL;
	}

	r = get_dtype((char *)param, &reg->dtype);
	xmlFree(param);
	if (r < 0)
		return r;

	/* parse: access */
	param = xmlGetProp(node, (const xmlChar *)"access");
	if (!param) {
		ilerr__set("Malformed entry (%s, missing access)", id);
		return IL_EFAIL;
	}

	r = get_access((char *)param, &reg->access);
	xmlFree(param);
	if (r < 0)
		return r;

	/* parse: phyisical units (optional) */
	param = xmlGetProp(node, (const xmlChar *)"phy");
	if (param) {
		reg->phy = get_phy((char *)param);
		xmlFree(param);
	} else {
		reg->phy = IL_REG_PHY_NONE;
	}

	/* parse: storage (optional) */
	param = xmlGetProp(node, (const xmlChar *)"storage");
	if (param) {
		get_storage((char *)param, reg);
		reg->storage_valid = 1;
		xmlFree(param);
	} else {
		reg->storage_valid = 0;
	}

	/* parse: category ID (optional) */
	param = xmlGetProp(node, (const xmlChar *)"cat_id");
	if (param) {
		reg->cat_id = strdup((const char *)param);
		xmlFree(param);
	} else {
		reg->cat_id = NULL;
	}

	/* parse: sub-category ID (optional) */
	param = xmlGetProp(node, (const xmlChar *)"scat_id");
	if (param) {
		if (!reg->cat_id) {
			ilerr__set("Subcategory %s requires a category", param);
			xmlFree(param);
			return IL_EFAIL;
		}

		reg->scat_id = strdup((const char *)param);
		xmlFree(param);
	} else {
		reg->scat_id = NULL;
	}

	/* parse: internal_use (optional) */
	param = xmlGetProp(node, (const xmlChar *)"internal_use");
	if (param) {
		reg->internal_use = 1;
	} else {
		reg->internal_use = 0;
	}

	/* assign default min/max */
	switch (reg->dtype) {
	case IL_REG_DTYPE_U8:
		reg->range.min.u8 = 0;
		reg->range.max.u8 = UINT8_MAX;
		break;
	case IL_REG_DTYPE_S8:
		reg->range.min.s8 = INT8_MIN;
		reg->range.max.s8 = INT8_MAX;
		break;
	case IL_REG_DTYPE_U16:
		reg->range.min.u16 = 0;
		reg->range.max.u16 = UINT16_MAX;
		break;
	case IL_REG_DTYPE_S16:
		reg->range.min.s16 = INT16_MIN;
		reg->range.max.s16 = INT16_MAX;
		break;
	case IL_REG_DTYPE_U32:
		reg->range.min.u32 = 0;
		reg->range.max.u32 = UINT32_MAX;
		break;
	case IL_REG_DTYPE_S32:
		reg->range.min.s32 = INT32_MIN;
		reg->range.max.s32 = INT32_MAX;
		break;
	case IL_REG_DTYPE_U64:
		reg->range.min.u64 = 0;
		reg->range.max.u64 = UINT64_MAX;
		break;
	case IL_REG_DTYPE_S64:
		reg->range.min.s64 = INT64_MIN;
		reg->range.max.s64 = INT64_MAX;
		break;
	case IL_REG_DTYPE_FLOAT:
		reg->range.min.flt = INT32_MIN;
		reg->range.max.flt = INT32_MAX;
		break;
	default:
		break;
	}

	/* parse: nested properties (e.g. labels, ranges, etc.) */
	return parse_reg_props(node, reg);
}

/*******************************************************************************
 * Public
 ******************************************************************************/

il_dict_t *il_dict_create(const char *dict_f)
{
	int r = 0, i, j;

	il_dict_t *dict;

	xmlXPathContextPtr xpath;
	xmlXPathObjectPtr obj_cats, obj_regs, obj_axes, obj_ver;
	xmlNodePtr root;

	khint_t k;


	dict = malloc(sizeof(*dict));
	if (!dict) {
		ilerr__set("Dictionary allocation failed");
		return NULL;
	}

	/* create hash table for categories and registers */
	dict->h_cats = kh_init(cat_id);
	if (!dict->h_cats) {
		ilerr__set("Categories hash table allocation failed");
		r = IL_EFAIL;
		goto cleanup_dict;
	}

	/* set library error function (to prevent stdout/stderr garbage) */
	xmlSetGenericErrorFunc(NULL, xml_error);

	/* initialize parser context and parse dictionary */
	dict->xml_ctxt = xmlNewParserCtxt();
	if (!dict->xml_ctxt) {
		ilerr__set("XML context allocation failed");
		r = IL_EFAIL;
		goto cleanup_h_regs;
	}

	dict->xml_doc = xmlCtxtReadFile(dict->xml_ctxt, dict_f, NULL, 0);
	if (!dict->xml_doc) {
		ilerr__set("xml: %s",
			   xmlCtxtGetLastError(dict->xml_ctxt)->message);
		r = IL_EFAIL;
		goto cleanup_xml_ctxt;
	}

	/* verify root */
	root = xmlDocGetRootElement(dict->xml_doc);
	if (xmlStrcmp(root->name, (const xmlChar *)ROOT_NAME) != 0) {
		ilerr__set("Unsupported dictionary format");
		r = IL_EFAIL;
		goto cleanup_xml_doc;
	}

	/* create a new XPath context */
	xpath = xmlXPathNewContext(dict->xml_doc);
	if (!xpath) {
		ilerr__set("xml: %s",
			   xmlCtxtGetLastError(dict->xml_ctxt)->message);
		r = IL_EFAIL;
		goto cleanup_xml_doc;
	}

	obj_ver = xmlXPathEvalExpression((const xmlChar *)XPATH_VERSION, xpath);
	if (!obj_ver) {
		ilerr__set("xml: %s",
			   xmlCtxtGetLastError(dict->xml_ctxt)->message);
		r = IL_EFAIL;
		goto cleanup_xpath;
	}

	xmlNodePtr node_ver = obj_ver->nodesetval->nodeTab[0];
	dict->version = node_ver->children[0].content;

	/* evaluate XPath for categories */
	obj_cats = xmlXPathEvalExpression((const xmlChar *)XPATH_CATS, xpath);
	if (!obj_cats) {
		ilerr__set("xml: %s",
			   xmlCtxtGetLastError(dict->xml_ctxt)->message);
		r = IL_EFAIL;
		goto cleanup_xpath;
	}

	/* parse each category */
	for (i = 0; i < obj_cats->nodesetval->nodeNr; i++) {
		xmlNodePtr node = obj_cats->nodesetval->nodeTab[i];

		r = parse_cat(node, dict);
		if (r < 0)
			goto cleanup_h_cats_entries;
	}

	/* evaluate XPath for axes */
	obj_axes = xmlXPathEvalExpression((const xmlChar *)XPATH_AXES, xpath);
	if (!obj_axes || obj_axes->nodesetval->nodeNr == 0) {
		// single-axis device
		khash_t(cat_id) *h_regs;
		dict->subnodes = INITIAL_SUBNODES;
		dict->h_regs = malloc(INITIAL_SUBNODES * sizeof(h_regs));
		/* parse each axis */
		for (i = 0; i < INITIAL_SUBNODES; i++) {
			dict->h_regs[i] = kh_init(reg_id);
			if (!dict->h_regs[i]) {
				ilerr__set("Registers hash table allocation failed");
				r = IL_EFAIL;
				goto cleanup_h_cats;
			}
		}
		/* parse each register */
		obj_regs = xmlXPathEvalExpression((const xmlChar *)XPATH_REGS, xpath);
		for (i = 0; i < obj_regs->nodesetval->nodeNr; i++) {
			xmlNodePtr node = obj_regs->nodesetval->nodeTab[i];
			int subnode = 1;
			r = parse_reg(node, dict, &subnode);
			if (r < 0)
				goto cleanup_h_regs_entries;
		}
	}
	else {
		// multiaxis device
		xmlNode *cur_node = NULL;

		/* allocate each axis */
		khash_t(cat_id) *h_regs;
		uint8_t num_axis = obj_axes->nodesetval->nodeNr;
		dict->subnodes = num_axis;
		dict->h_regs = malloc(num_axis * sizeof(h_regs));
		/* parse each axis */
		for (i = 0; i < num_axis; i++) {
			dict->h_regs[i] = kh_init(reg_id);
			if (!dict->h_regs[i]) {
				ilerr__set("Registers hash table allocation failed");
				r = IL_EFAIL;
				goto cleanup_h_cats;
			}

			xmlNodePtr axis_node = obj_axes->nodesetval->nodeTab[i];
			xmlNodePtr registers_node = axis_node->children->next->children;

			for (cur_node = registers_node; cur_node; cur_node = cur_node->next) {
				if (cur_node->type == XML_ELEMENT_NODE) {
					int subnode = 1;
					r = parse_reg(cur_node, dict, &subnode);
					if (r < 0)
						goto cleanup_h_regs_entries;
				}
			}
		}
	}

	/* evaluate XPath for registers */
	obj_regs = xmlXPathEvalExpression((const xmlChar *)XPATH_REGS, xpath);
	if (!obj_regs) {
		ilerr__set("xml: %s",
			   xmlCtxtGetLastError(dict->xml_ctxt)->message);
		r = IL_EFAIL;
		goto cleanup_h_cats;
	}

	xmlXPathFreeObject(obj_regs);
	xmlXPathFreeObject(obj_cats);
	xmlXPathFreeObject(obj_ver);

	goto cleanup_xpath;

cleanup_h_regs_entries:
	for (k = 0; k < kh_end(dict->h_regs[0]); ++k) {
		if (kh_exist(dict->h_regs[0], k)) {
			il_reg_t *reg;

			reg = &kh_value(dict->h_regs[0], k).reg;
			if (reg->cat_id)
				free((char *)reg->cat_id);
			if (reg->scat_id)
				free((char *)reg->scat_id);
			if (reg->labels)
				il_dict_labels_destroy(reg->labels);

			xmlFree((char *)kh_key(dict->h_regs[0], k));
		}
	}

	xmlXPathFreeObject(obj_regs);

cleanup_h_cats_entries:
	for (k = 0; k < kh_end(dict->h_cats); ++k) {
		il_dict_labels_t *labels;
		khint_t j;

		khash_t(scat_id) * h_scats;

		if (!kh_exist(dict->h_cats, k))
			continue;

		/* clear labels */
		labels = kh_value(dict->h_cats, k).labels;
		if (labels)
			il_dict_labels_destroy(labels);

		/* clear subcategories */
		h_scats = kh_value(dict->h_cats, k).h_scats;
		if (h_scats) {
			for (j = 0; j < kh_end(h_scats); ++j) {
				if (!kh_exist(h_scats, j))
					continue;

				/* clear labels */
				labels = kh_value(h_scats, j);
				if (labels)
					il_dict_labels_destroy(labels);

				xmlFree((char *)kh_key(h_scats, j));
			}

			kh_destroy(scat_id, h_scats);
		}

		xmlFree((char *)kh_key(dict->h_cats, k));
	}

	xmlXPathFreeObject(obj_cats);

cleanup_xpath:
	xmlXPathFreeContext(xpath);

cleanup_xml_doc:
	if (r < 0)
		xmlFreeDoc(dict->xml_doc);

cleanup_xml_ctxt:
	if (r < 0)
		xmlFreeParserCtxt(dict->xml_ctxt);

cleanup_h_regs:
	if (r < 0)
		kh_destroy(reg_id, dict->h_regs);

cleanup_h_cats:
	if (r < 0)
		kh_destroy(cat_id, dict->h_cats);

cleanup_h_axes:

cleanup_dict:
	if (r < 0) {
		free(dict);
		return NULL;
	}

	return dict;
}

void il_dict_destroy(il_dict_t *dict)
{
	khint_t k;

	for (k = 0; k < kh_end(dict->h_regs[0]); ++k) {
		if (kh_exist(dict->h_regs[0], k)) {
			il_reg_t *reg;

			reg = &kh_value(dict->h_regs[0], k).reg;
			if (reg->cat_id)
				free((char *)reg->cat_id);
			if (reg->scat_id)
				free((char *)reg->scat_id);
			if (reg->labels)
				il_dict_labels_destroy(reg->labels);

			xmlFree((char *)kh_key(dict->h_regs[0], k));
		}
	}

	kh_destroy(reg_id, dict->h_regs[0]);

	for (k = 0; k < kh_end(dict->h_cats); ++k) {
		il_dict_labels_t *labels;
		khint_t j;

		khash_t(scat_id) * h_scats;

		if (!kh_exist(dict->h_cats, k))
			continue;

		/* clear labels */
		labels = kh_value(dict->h_cats, k).labels;
		if (labels)
			il_dict_labels_destroy(labels);

		/* clear subcategories */
		h_scats = kh_value(dict->h_cats, k).h_scats;
		if (h_scats) {
			for (j = 0; j < kh_end(h_scats); ++j) {
				if (!kh_exist(h_scats, j))
					continue;

				/* clear labels */
				labels = kh_value(h_scats, j);
				if (labels)
					il_dict_labels_destroy(labels);

				xmlFree((char *)kh_key(h_scats, j));
			}

			kh_destroy(scat_id, h_scats);
		}

		xmlFree((char *)kh_key(dict->h_cats, k));
	}

	kh_destroy(cat_id, dict->h_cats);

	xmlFreeDoc(dict->xml_doc);
	xmlFreeParserCtxt(dict->xml_ctxt);

	free(dict);
}

int il_dict_save(il_dict_t *dict, const char *fname)
{
	if (xmlSaveFile(fname, dict->xml_doc) < 0) {
		ilerr__set("xml: %s",
			   xmlCtxtGetLastError(dict->xml_ctxt)->message);
		return IL_EFAIL;
	}

	return 0;
}

int il_dict_cat_get(il_dict_t *dict, const char *cat_id,
		    il_dict_labels_t **labels)
{
	khint_t k;

	k = kh_get(cat_id, dict->h_cats, cat_id);
	if (k == kh_end(dict->h_cats)) {
		ilerr__set("Category not found (%s)", cat_id);
		return IL_EFAIL;
	}

	*labels = kh_value(dict->h_cats, k).labels;

	return 0;
}

size_t il_dict_cat_cnt(il_dict_t *dict)
{
	return (size_t)kh_size(dict->h_cats);
}

const char **il_dict_cat_ids_get(il_dict_t *dict)
{
	const char **ids;
	size_t i;
	khint_t k;

	/* allocate array for category keys */
	ids = malloc(sizeof(const char *) * (il_dict_cat_cnt(dict) + 1));
	if (!ids) {
		ilerr__set("Categories array allocation failed");
		return NULL;
	}

	/* assign keys, null-terminate */
	for (i = 0, k = 0; k < kh_end(dict->h_cats); ++k) {
		if (kh_exist(dict->h_cats, k)) {
			ids[i] = (const char *)kh_key(dict->h_cats, k);
			i++;
		}
	}

	ids[i] = NULL;

	return ids;
}

void il_dict_cat_ids_destroy(const char **cat_ids)
{
	free((char **)cat_ids);
}

int il_dict_scat_get(il_dict_t *dict, const char *cat_id, const char *scat_id,
		     il_dict_labels_t **labels)
{
	khint_t k, j;

	khash_t(scat_id) * h_scats;

	k = kh_get(cat_id, dict->h_cats, cat_id);
	if (k == kh_end(dict->h_cats)) {
		ilerr__set("Category not found (%s)", cat_id);
		return IL_EFAIL;
	}

	h_scats = kh_value(dict->h_cats, k).h_scats;

	j = kh_get(scat_id, h_scats, scat_id);
	if (j == kh_end(h_scats)) {
		ilerr__set("Sub-category not found (%s)", scat_id);
		return IL_EFAIL;
	}

	*labels = kh_value(h_scats, j);

	return 0;
}

size_t il_dict_scat_cnt(il_dict_t *dict, const char *cat_id)
{
	khint_t k;

	khash_t(scat_id) * h_scats;

	k = kh_get(cat_id, dict->h_cats, cat_id);
	if (k == kh_end(dict->h_cats))
		return 0;

	h_scats = kh_value(dict->h_cats, k).h_scats;

	return (size_t)kh_size(h_scats);
}

const char **il_dict_scat_ids_get(il_dict_t *dict, const char *cat_id)
{
	const char **ids;
	size_t i, scats_cnt;
	khint_t k;

	khash_t(scat_id) * h_scats;

	/* obtain subcategories dictionary */
	k = kh_get(cat_id, dict->h_cats, cat_id);
	if (k == kh_end(dict->h_cats))
		return NULL;

	h_scats = kh_value(dict->h_cats, k).h_scats;
	scats_cnt = (size_t)kh_size(h_scats);

	/* allocate array for category keys */
	ids = malloc(sizeof(const char *) * (scats_cnt + 1));
	if (!ids) {
		ilerr__set("Categories array allocation failed");
		return NULL;
	}

	/* assign keys, null-terminate */
	for (i = 0, k = 0; k < kh_end(h_scats); ++k) {
		if (kh_exist(h_scats, k)) {
			ids[i] = (const char *)kh_key(h_scats, k);
			i++;
		}
	}

	ids[i] = NULL;

	return ids;
}

void il_dict_scat_ids_destroy(const char **ids)
{
	free((char **)ids);
}

int il_dict_reg_get(il_dict_t *dict, const char *id, const il_reg_t **reg, uint8_t subnode)
{
	khint_t k;

	k = kh_get(reg_id, dict->h_regs[subnode], id);
	if (k == kh_end(dict->h_regs[subnode])) {
		ilerr__set("Register not found (%s)", id);
		return IL_EFAIL;
	}

	*reg = (const il_reg_t *)&kh_value(dict->h_regs[subnode], k).reg;

	return 0;
}

size_t il_dict_reg_cnt(il_dict_t *dict, uint8_t subnode)
{
	return (size_t)kh_size(dict->h_regs[subnode]);
}

int il_dict_reg_storage_update(il_dict_t *dict, const char *id,
			       il_reg_value_t storage, uint8_t subnode)
{
	khint_t k;
	char value[NUM_STR_LEN];
	il_reg_t *reg;
	xmlNodePtr node;

	k = kh_get(reg_id, dict->h_regs[subnode], id);
	if (k == kh_end(dict->h_regs[subnode])) {
		ilerr__set("Register not found (%s)", id);
		return IL_EFAIL;
	}

	/* update register */
	reg = &kh_value(dict->h_regs[subnode], k).reg;
	reg->storage = storage;
	reg->storage_valid = 1;

	/* update XML node */
	switch (reg->dtype) {
	case IL_REG_DTYPE_U8:
		snprintf(value, sizeof(value), "%" PRIu8, storage.u8);
		break;
	case IL_REG_DTYPE_S8:
		snprintf(value, sizeof(value), "%" PRId8, storage.s8);
		break;
	case IL_REG_DTYPE_U16:
		snprintf(value, sizeof(value), "%" PRIu16, storage.u16);
		break;
	case IL_REG_DTYPE_S16:
		snprintf(value, sizeof(value), "%" PRId16, storage.s16);
		break;
	case IL_REG_DTYPE_U32:
		snprintf(value, sizeof(value), "%" PRIu32, storage.u32);
		break;
	case IL_REG_DTYPE_S32:
		snprintf(value, sizeof(value), "%" PRId32, storage.s32);
		break;
	case IL_REG_DTYPE_U64:
		snprintf(value, sizeof(value), "%" PRIu64, storage.u64);
		break;
	case IL_REG_DTYPE_S64:
		snprintf(value, sizeof(value), "%" PRId64, storage.s64);
		break;
	case IL_REG_DTYPE_FLOAT:
		snprintf(value, sizeof(value), "%f", storage.flt);
		break;
	default:
		break;
	}

	node = kh_value(dict->h_regs[subnode], k).xml_node;
	(void)xmlSetProp(node, (const xmlChar *)"storage",
			 (const xmlChar *)value);

	return 0;
}

const char **il_dict_reg_ids_get(il_dict_t *dict, uint8_t subnode)
{
	const char **ids;
	size_t i;
	khint_t k;

	/* allocate array for register keys */
	ids = malloc(sizeof(const char *) * (il_dict_reg_cnt(dict, subnode) + 1));
	if (!ids) {
		ilerr__set("Registers array allocation failed");
		return NULL;
	}

	/* assign keys, null-terminate */
	for (i = 0, k = 0; k < kh_end(dict->h_regs[subnode]); ++k) {
		if (kh_exist(dict->h_regs[subnode], k)) {
			ids[i] = (const char *)kh_key(dict->h_regs[subnode], k);
			i++;
		}
	}

	ids[i] = NULL;

	return ids;
}

void il_dict_reg_ids_destroy(const char **ids)
{
	free((char **)ids);
}

const char *il_dict_version_get(il_dict_t *dict)
{
	return dict->version;
}

int il_dict_subnodes_get(il_dict_t *dict)
{
	return dict->subnodes;
}
