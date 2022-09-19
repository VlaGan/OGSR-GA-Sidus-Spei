////////////////////////////////////////////////////////////////////////////
//	Module 		: object_item_script.cpp
//	Created 	: 27.05.2004
//  Modified 	: 30.06.2004
//	Author		: Dmitriy Iassenev
//	Description : Object item script class
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "object_item_script.h"
#include "object_factory.h"

#include "attachable_item.h"

ObjectFactory::CLIENT_BASE_CLASS* CObjectItemScript::client_object() const
{
    ObjectFactory::CLIENT_SCRIPT_BASE_CLASS* object;
    // try {
#ifdef LUABIND_09
    object = luabind::object_cast<ObjectFactory::CLIENT_SCRIPT_BASE_CLASS*>(m_client_creator(), luabind::adopt(luabind::result));
#else
    object = luabind::object_cast<ObjectFactory::CLIENT_SCRIPT_BASE_CLASS*>(m_client_creator(), luabind::adopt<luabind::result>());
#endif
    //}
    // catch(...) {
    //	return	(0);
    //}
    R_ASSERT(object);
    return (object->_construct());
}

ObjectFactory::SERVER_BASE_CLASS* CObjectItemScript::server_object(LPCSTR section) const
{
    typedef ObjectFactory::SERVER_SCRIPT_BASE_CLASS SERVER_SCRIPT_BASE_CLASS;
    typedef ObjectFactory::SERVER_BASE_CLASS SERVER_BASE_CLASS;
    SERVER_SCRIPT_BASE_CLASS* object;

    // try {
    luabind::object* instance = 0;
    // try {
    instance = xr_new<luabind::object>((luabind::object)(m_server_creator(section)));
    //}
    // catch(std::exception& e) {
    //	Msg			("Exception [%s] raised while creating server object from section [%s]", e.what(),section);
    //	return		(0);
    //}
    // catch(...) {
    //	Msg			("Exception raised while creating server object from section [%s]",section);
    //	return		(0);
    //}
#ifdef LUABIND_09
    object = luabind::object_cast<ObjectFactory::SERVER_SCRIPT_BASE_CLASS*>(*instance, luabind::adopt(luabind::result));
#else
    object = luabind::object_cast<ObjectFactory::SERVER_SCRIPT_BASE_CLASS*>(*instance, luabind::adopt<luabind::result>());
#endif
    xr_delete(instance);
    //}
    // catch(std::exception& e) {
    //	Msg				("Exception [%s] raised while casting and adopting script server object from section [%s]", e.what(),section);
    //	return			(0);
    //}
    // catch(...) {
    //	Msg				("Exception raised while creating script server object from section [%s]", section);
    //	return			(0);
    //}

    R_ASSERT(object);
    SERVER_BASE_CLASS* o = object->init();
    R_ASSERT(o);
    return (o);
}

CObjectItemScript::CObjectItemScript(luabind::object client_creator, luabind::object server_creator, const CLASS_ID& clsid, LPCSTR script_clsid) : inherited(clsid, script_clsid)
{
    m_client_creator = client_creator;
    m_server_creator = server_creator;
}

CObjectItemScript::CObjectItemScript(luabind::object unknown_creator, const CLASS_ID& clsid, LPCSTR script_clsid) : inherited(clsid, script_clsid)
{
    m_client_creator = m_server_creator = unknown_creator;
}
