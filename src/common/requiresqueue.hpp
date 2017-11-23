//  OpenVPN 3 Linux client -- Next generation OpenVPN client
//
//  Copyright (C) 2017      OpenVPN Inc. <sales@openvpn.net>
//  Copyright (C) 2017      David Sommerseth <davids@openvpn.net>
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, version 3 of the License
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#ifndef OPENVPN3_DBUS_REQUIRESQUEUE_HPP
#define OPENVPN3_DBUS_REQUIRESQUEUE_HPP

#include <iostream>
#include <sstream>
#include <map>
#include <vector>
#include <algorithm>
#include <exception>
#include <cassert>

#include "dbus/core.hpp"


struct RequiresSlot
{
    unsigned int id;
    ClientAttentionType type;
    ClientAttentionGroup group;
    std::string name;
    std::string value;
    std::string user_description;
    bool hidden_input;
    bool provided;
};


class RequiresQueueException : public std::exception
{
public:
    RequiresQueueException(std::string err)
        : error(err)
    {
    }

    RequiresQueueException(std::string errname, std::string errmsg)
        : error(errmsg),
          errorname(errname)
    {
    }

    virtual ~RequiresQueueException() throw() {}

    virtual const char* what() const throw()
    {
        std::stringstream ret;
        ret << "[RequireQueryException"
            << "] " << error;
        return ret.str().c_str();
    }

    const std::string& err() const noexcept
    {
        std::string ret(error);
        return std::move(ret);
    }

    void GenerateDBusError(GDBusMethodInvocation *invocation)
    {
        std::string errnam = (!errorname.empty() ? errorname : "net.openvpn.v3.error.undefined");
        GError *dbuserr = g_dbus_error_new_for_dbus_error(errnam.c_str(), error.c_str());
        g_dbus_method_invocation_return_gerror(invocation, dbuserr);
        g_error_free(dbuserr);
    }
private:
    std::string error;
    std::string errorname;
};


class RequiresQueue
{
public:
    typedef std::tuple<ClientAttentionType, ClientAttentionGroup> ClientAttTypeGroup;

    RequiresQueue()
        : reqids()
    {
    };

    ~RequiresQueue()
    {
    }

    /**
     * Returns a string containing a D-Bus introspection section for the
     * RequiresQueue methods available via D-Bus.  The method names provided
     * are the ones needed to be be used on on the D-Bus.
     *
     * @param meth_qchktypegr    A string with the method name for getting
     *                           a list of unprocessed requirement type/groups
     * @param meth_queuefetch    A string with the method name for fetching an
     *                           unprocessed queued element.
     * @param meth_queuechk      A string with the method name for getting
     *                           the number of unprocessed queued elements.
     * @param meth_provideresp   A string with the method name for providing
     *                           user responses to the service.
     *
     * @return  Returns a string with the various <method/> tags describing
     *          the required input arguments and what these methods returns.
     */
    std::string IntrospectionMethods(const std::string meth_qchktypegr,
                                     const std::string meth_queuefetch,
                                     const std::string meth_queuechk,
                                     const std::string meth_provideresp)
    {
        std::stringstream introspection;
        introspection << "    <method name='" << meth_qchktypegr << "'>"
                      << "      <arg type='a(uu)' name='type_group_list' direction='out'/>"
                      << "    </method>"
                      << "    <method name='" << meth_queuefetch << "'>"
                      << "      <arg type='u' name='type' direction='in'/>"
                      << "      <arg type='u' name='group' direction='in'/>"
                      << "      <arg type='u' name='id' direction='in'/>"
                      << "      <arg type='u' name='type' direction='out'/>"
                      << "      <arg type='u' name='group' direction='out'/>"
                      << "      <arg type='u' name='id' direction='out'/>"
                      << "      <arg type='s' name='name' direction='out'/>"
                      << "      <arg type='s' name='description' direction='out'/>"
                      << "      <arg type='b' name='hidden_input' direction='out'/>"
                      << "    </method>"
                      << "    <method name='" << meth_queuechk << "'>"
                      << "      <arg type='u' name='type' direction='in'/>"
                      << "      <arg type='u' name='group' direction='in'/>"
                      << "      <arg type='au' name='indexes' direction='out'/>"
                      << "    </method>"
                      << "    <method name='" << meth_provideresp << "'>"
                      << "      <arg type='u' name='type' direction='in'/>"
                      << "      <arg type='u' name='group' direction='in'/>"
                      << "      <arg type='u' name='id' direction='in'/>"
                      << "      <arg type='s' name='value' direction='in'/>"
                      << "    </method>";
        return introspection.str();
    }

    /**
     * Adds a user request requirement to the queue.
     *
     * The type and group arguments allows a single RequiresQueue object to
     * process multiple queues in parallel and also be available over D-Bus.
     *
     * @param type   ClientAttentionType reference
     * @param group  ClientAttentionGroup reference
     * @param name   String with a variable name for the requested input
     * @param descr  A human readable description of the value being
     *               requested.  This may be presented to the user directly.
     *
     * @return Returns the assigned ID for this requirement
     */
    unsigned int RequireAdd(ClientAttentionType type,
                    ClientAttentionGroup group,
                    std::string name,
                    std::string descr,
                    bool hidden_input)
    {
        struct RequiresSlot elmt = {0};
        elmt.id = reqids[get_reqid_index(type, group)]++;
        elmt.type = type;
        elmt.group = group;
        elmt.name = name;
        elmt.user_description = descr;
        elmt.provided = false;
        elmt.hidden_input = hidden_input;
        slots.push_back(elmt);

        return elmt.id;
    }


    /**
     *  Fetch a single element from the request queue.
     *
     *  @params invocattion  Pointer to the current GDBusMethodInvocation object
     *  @param  parameters   Pointer to the current GVariants object with the query parameters
     *
     *  Throws RequiresQueueException() when the queue is empty.
     *
     **/
    void QueueFetch(GDBusMethodInvocation *invocation, GVariant *parameters)
    {
        unsigned int type;
        unsigned int group;
        unsigned int id;
        g_variant_get(parameters, "(uuu)", &type, &group, &id);

        // Fetch the requested slot id
        for (auto& e : slots)
        {
            if (id == e.id)
            {
                if (e.type == (ClientAttentionType) type
                    && e.group == (ClientAttentionGroup) group)
                {
                    if (e.provided)
                    {
                        throw RequiresQueueException("net.openvpn.v3.already-provided",
                                                     "User input already provided");
                    }

                    GVariant *elmt = g_variant_new("(uuussb)",
                                                   e.type,
                                                   e.group,
                                                   e.id,
                                                   e.name.c_str(),
                                                   e.user_description.c_str(),
                                                   e.hidden_input);
                    g_dbus_method_invocation_return_value(invocation, elmt);
                    return;
                }
            }
        }
        throw RequiresQueueException("net.openvpn.v3.element-not-found",
                                     "No requires queue element found");
    }


    /**
     *  Updates a RequiresSlot element via D-Bus.  This method is intended
     *  to be called by D-Bus method callback function where both the
     *  element ID and the value is provided in a GVariant D-Bus object.
     *
     *  If the update fails, it will throw a RequiresQueueException error
     *  with the proper details.  It will also add a valid D-Bus error message
     *  to the invocation object whenever this event happens.
     *
     *  On success it will just return silently.
     *
     *  @param type      ClientAttentionType of the record to update
     *  @param group     ClientAttentionGroup the record belongs to
     *  @param id        Slot ID of the record to update
     *  @param newvalue  The new value for this record
     *
     */
    void UpdateEntry(ClientAttentionType type, ClientAttentionGroup group,
                     unsigned int id, std::string newvalue)
    {
        for (auto& e : slots)
        {
            if (e.type ==  type && e.group == (ClientAttentionGroup) group && e.id == id)
            {
                if (!e.provided)
                {
                    e.provided = true;
                    e.value = newvalue;
                    return;
                }
                else
                {
                    throw RequiresQueueException("net.openvpn.v3.error.input-already-provided",
                                                 "Request ID " + std::to_string(id) + " has already been provided");
                }
            }
        }
        throw RequiresQueueException("net.openvpn.v3.invalid-input",
                                     "No matching entry found in the request queue");
    }


    /**
     *  This is a D-Bus variant of @UpdateEntry().  This takes the
     *  D-Bus method call invocation and parameters provided with the call
     *  and parses them.  This information is then sent to the other
     *  @UpdateEntry() method for the real update.
     *
     *  If the update fails, it will throw a RequiresQueueException error
     *  with the proper details.
     *
     *  On success it will return an empty and successful D-Bus response.
     *
     *  @params invocation The GDBus invocation object, which will contain the
     *                     response on success.
     *  @params indata     A GVariant object containing the input data from
     *                     the D-Bus call
     *
     */
    void UpdateEntry(GDBusMethodInvocation *invocation, GVariant *indata)
    {
        //
        // Typically used by the function parsing user provided data
        // usually a backend process who asked for user input
        //
        unsigned int type;
        unsigned int group;
        guint id;
        gchar *value = NULL;
        g_variant_get(indata, "(uuus)",
                      &type,
                      &group,
                      &id,
                      &value);

        if (NULL == value)
        {
            throw RequiresQueueException("net.openvpn.v3.error.invalid-input",
                                         "No value provided for RequiresSlot ID " + std::to_string(id));
        }

        UpdateEntry((ClientAttentionType) type, (ClientAttentionGroup) group, id, std::string(value));
        g_dbus_method_invocation_return_value(invocation, NULL);
        g_free(value);  // Avoid leak
    }


    /**
     * Resets the value and the provided flag of an item already provided element
     *
     * @param type   ClientAttentionType which the value is categorised under
     * @param group  ClientAttentionGroup which the value is categorised under
     * @param id     The numeric ID of the value slot to reset
     *
     * @return Nothing on success.  If the value could not be found, an
     *         exception is thrown.
     */
    void ResetValue(ClientAttentionType type, ClientAttentionGroup group, unsigned int id)
    {
        for (auto& e : slots)
        {
            if (e.type == type && e.group == group && e.id == id)
            {
                e.provided = false;
                e.value = "";
                return;
            }
        }
        throw RequiresQueueException("No matching entry found in the request queue");
    }

    /**
     * Retrieve the value provided by a user
     *
     * @param type   ClientAttentionType which the value is categorised under
     * @param group  ClientAttentionGroup which the value is categorised under
     * @param id     The numeric ID of the value
     * @return Returns a string with the value if the value was found and
     *         provided by the user, otherwise an exception is thrown.
     */
    std::string GetResponse(ClientAttentionType type, ClientAttentionGroup group, unsigned int id)
    {
        for (auto& e : slots)
        {
            if (e.type == type && e.group == group && e.id == id)
            {
                if (!e.provided)
                {
                    throw RequiresQueueException("Request never provided by front-end");
                }
                return e.value;
            }
        }
        throw RequiresQueueException("No matching entry found in the request queue");
    }

    /**
     *
     * @param type   ClientAttentionType which the value is categorised under
     * @param group  ClientAttentionGroup which the value is categorised under
     * @param name   A string containing the variable name of the value
     * @return Returns a string with the value if the value was found and
     *         provided by the user, otherwise an exception is thrown.
     * @return
     */
    std::string GetResponse(ClientAttentionType type, ClientAttentionGroup group, std::string name)
    {
        for (auto& e : slots)
        {
            if (e.type == type && e.group == group && e.name == name)
            {
                if (!e.provided)
                {
                    throw RequiresQueueException("Request never provided by front-end");
                }
                return e.value;
            }
        }
        throw RequiresQueueException("No matching entry found in the request queue");
    }

    /**
     * Get the number of requires slots which have been prepared for a
     * specific client attention type and group.
     *
     * @param type   ClientAttentionType the value to count belongs to
     * @param group  ClientAttentionGroup the value to count belongs to
     * @return Returns the number of requires slots prepared
     */
    unsigned int QueueCount(ClientAttentionType type, ClientAttentionGroup group)
    {
        unsigned int ret = 0;
        for (auto& e : slots)
        {
            if (type == e.type && group == e.group)
            {
                ret++;
            }
        }
        return ret;
    }


    /**
     * Returns a list of ClientAttentionType/ClientAttentionGroup tuples
     * of requirements which have not been provided yet.  This information
     * can further be used with @QueueCheck() to get a list of requirement
     * IDs not satisfied.  Then in the end @QueueFetch() is used to get
     * details about a specific requirement
     *
     * @return Returns a std::vector<ClientAttTypeGroup>
     *         of type/groups not yet satisfied.
     */
    std::vector<ClientAttTypeGroup> QueueCheckTypeGroup()
    {
        std::vector<ClientAttTypeGroup> ret;

        for (auto& e : slots)
        {
            if (!e.provided)
            {
                // Check if we've already spotted this type/group
                bool found = false;
                for (auto& r : ret)
                {
                    ClientAttentionType t;
                    ClientAttentionGroup g;
                    std::tie(t, g) = r;
                    if (t == e.type && g == e.group)
                    {
                        // yes, we have
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    ret.push_back(std::make_tuple(e.type, e.group));
                }
            }
        }
        return ret;
    }

    /**
     * D-Bus wrapper around @QueueCheckTypeGroup().  Returns the result
     * to an on-going D-Bus method call
     *
     * @param GDBusMethodInvocation Pointer to a D-Bus invocation, where the
     *                              result will be returned on success
     */
    void QueueCheckTypeGroup(GDBusMethodInvocation *invocation)
    {
        // Convert the std::vector to a GVariant based array GDBus can use
        // as the method call response
        std::vector<std::tuple<ClientAttentionType, ClientAttentionGroup>> qchk_res = QueueCheckTypeGroup();

        GVariantBuilder *bld = g_variant_builder_new(G_VARIANT_TYPE("a(uu)"));
        assert(NULL != bld);
        for (auto& e : qchk_res)
        {
            ClientAttentionType t;
            ClientAttentionGroup g;
            std::tie(t, g) = e;
            g_variant_builder_add(bld, "(uu)", (unsigned int) t, (unsigned int) g);
        }

        // Wrap the GVariant array into a tuple which GDBus expects
        GVariantBuilder *ret = g_variant_builder_new(G_VARIANT_TYPE_TUPLE);
        g_variant_builder_add_value(ret, g_variant_builder_end(bld));
        g_dbus_method_invocation_return_value(invocation, g_variant_builder_end(ret));

        // Clean-up GVariant builders
        g_variant_builder_unref(bld);
        g_variant_builder_unref(ret);
    }


    /**
     * Retrieve a list of ID references of require slots which have not
     * received any user responses.
     *
     * @param type   ClientAttentionType of the queue to check
     * @param group  ClientAttentionGroup of the queue to check
     * @return Returns an array of unsigned integers with IDs to variables
     *         still not been provided by the user
     */
    std::vector<unsigned int> QueueCheck(ClientAttentionType type, ClientAttentionGroup group)
    {
        std::vector<unsigned int> ret;
        for (auto& e : slots)
        {
            if (type == e.type
                && group == e.group
                && !e.provided)
            {
                ret.push_back(e.id);
            }
        }
        return ret;
    }

    /**
     * Retrieve a list of ID references of require slots which have not
     * received any user responses.  This variant is a wrapper to be used
     * by D-Bus methods.  The GVariant pointer needs to point at a tuple
     * containing two unsigned integers - (uu); and need to valid references
     * to a ClientAttentionType and ClientAttentionGroup.
     *
     *
     * @param GDBusMethodInvocation Pointer to a D-Bus invocation, where the
     *                              result will be returned on success
     * @param GVariant              Pointer to the D-Bus method call
     *                              parameters.
     *                              Must reference a valid ClientAttentionType
     *                              and ClientAttentionGroup
     */
    void QueueCheck(GDBusMethodInvocation *invocation, GVariant *parameters)
    {
        unsigned int type;
        unsigned int group;
        g_variant_get(parameters, "(uu)", &type, &group);

        // Convert the std::vector to a GVariant based array GDBus can use
        // as the method call response
        std::vector<unsigned int> qchk_result = QueueCheck((ClientAttentionType) type, (ClientAttentionGroup) group);
        GVariantBuilder *bld = g_variant_builder_new(G_VARIANT_TYPE("au"));
        for (auto& e : qchk_result)
        {
            g_variant_builder_add(bld, "u", e);
        }

        // Wrap the GVariant array into a tuple which GDBus expects
        GVariantBuilder *ret = g_variant_builder_new(G_VARIANT_TYPE_TUPLE);
        g_variant_builder_add_value(ret, g_variant_builder_end(bld));
        g_dbus_method_invocation_return_value(invocation, g_variant_builder_end(ret));

        // Clean-up GVariant builders
        g_variant_builder_unref(bld);
        g_variant_builder_unref(ret);
    }

    /**
     * Counts all requires slots which have not received any user input
     *
     * @return Returns an unsigned integer of slots lacking user responses
     */
    unsigned int QueueCheckAll()
    {
        unsigned int ret = 0;
        for (auto& e : slots)
        {
            if (!e.provided)
            {
                ret++;
            }
        }
        return ret;
    }

    /**
     * Simple wrapper around @QueueCheckAll which only returns true or false
     *
     * @return Returns true if all requires slots has been satisfied
     *         successfully
     */
    bool QueueAllDone()
        {
            return QueueCheckAll() == 0;
        }

    /**
     * Checks if a ClientAttentionType and ClientAttentionGroup is fully
     * satisfied with all slots containing user provided values.
     *
     * @param type   ClientAttentionType to check
     * @param group  ClientAttentionGroup to check
     *
     * @return Returns true if all slots have valid responses.
     */
    bool QueueDone(ClientAttentionType type, ClientAttentionGroup group)
    {
        return QueueCheck(type, group).size() == 0;
    }

    /**
     * Checks if a ClientAttentionType and ClientAttentionGroup is fully
     * satisfied with all slots containing user provided values.
     * This is a D-Bus variant, which takes the same GVariant data type as the
     * @UpdateEntry method, which needs to contain type, group, id and value.
     * Only the type and group references are used by this function.
     *
     * @param parameters  GVariant pointer to the D-Bus method call arguments
     * @return Returns true if all slots have received user input responses,
     *         otherwise false.
     */
    bool QueueDone(GVariant *parameters)
    {
        // First, grab the slot ID ...
        unsigned int type;
        unsigned int group;
        unsigned int id;
        gchar *value;
        g_variant_get(parameters, "(uuus)", &type, &group, &id, &value);

        return QueueCheck((ClientAttentionType) type, (ClientAttentionGroup) group).size() == 0;
    }

#ifdef DEBUG_REQUIRESQUEUE
    void _DumpStdout()
    {
         _DumpQueue(std::cout);
    }


    void _DumpQueue(std::ostream& logdst)
    {
        for (auto& e : slots)
        {
        logdst << "          Id: " << e.id << std::endl
               << "         Key: " << e.name << std::endl
               << "        Type: [" << std::to_string((int) e.type) << "] "
               << ClientAttentionType_str[(int)e.type] << std::endl
               << "       Group: [" << std::to_string((int) e.group) << "] "
               << ClientAttentionGroup_str[(int)e.group] << std::endl
               << "       Value: " << e.value << std::endl
               << " Description: " << e.user_description << std::endl
               << "Hidden input: " << (e.hidden_input ? "True": "False")
               << std::endl
               << "    Provided: " << (e.provided ? "True": "False")
               << std::endl
               << "-----------------------------------------------------"
               << std::endl;
        }
    }
#endif

private:
    std::map<unsigned int, unsigned int> reqids;
    std::vector<struct RequiresSlot> slots;


    /**
     * Simple index hashing to be used by a single dimensional integer
     * array/table.  This is used to have a unique single ID per type:group,
     * used for a table keeping track of slot IDs used within each type:group.
     *
     * @param type   ClientAttentionType reference
     * @param group  ClientAttentionGroup reference
     *
     * @return  Returns a unique index based on the two input arguments
     */
    unsigned int get_reqid_index(ClientAttentionType type, ClientAttentionGroup group)
    {
        return ((unsigned int)type *100) + (unsigned int)group;

    }

};

#endif // OPENVPN3_DBUS_REQUIRESQUEUE_HPP

