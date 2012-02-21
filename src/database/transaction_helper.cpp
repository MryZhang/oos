/*
 * This file is part of OpenObjectStore OOS.
 *
 * OpenObjectStore OOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenObjectStore OOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenObjectStore OOS. If not, see <http://www.gnu.org/licenses/>.
 */

#include "database/transaction_helper.hpp"

#include "object/object_store.hpp"
#include "object/object.hpp"

namespace oos {

bool backup_visitor::backup(action *act, byte_buffer *buffer)
{
  buffer_ = buffer;
  act->accept(this);
  buffer_ = NULL;
  return true;
}

void backup_visitor::visit(insert_action *)
{
  // nothing to do
}

void backup_visitor::visit(update_action *a)
{
  // serialize object
  serializer_.serialize(a->obj(), *buffer_);
}

void backup_visitor::visit(delete_action *a)
{
  // serialize object
  serializer_.serialize(a->obj(), *buffer_);
}

bool restore_visitor::restore(action *act, byte_buffer *buffer, object_store *ostore)
{
  ostore_ = ostore;
  buffer_ = buffer;
  act->accept(this);
  buffer_ = NULL;
  ostore_ = NULL;
  return true;
}

void restore_visitor::visit(insert_action *a)
{
  // remove object from object store
  for (insert_action::iterator i = a->begin(); i != a->end(); ++i) {
    ostore_->remove_object(*i, false);
  }
}

void restore_visitor::visit(update_action *a)
{
  // deserialize data from buffer into object
  serializer_.deserialize(a->obj(), *buffer_, ostore_);
}

void restore_visitor::visit(delete_action *a)
{
  // check if there is an object with id in
  // object store
  object_proxy *oproxy = ostore_->find_proxy(a->id());
  if (!oproxy) {
    // create proxy
    oproxy = ostore_->create_proxy(a->id());
  }
  if (!oproxy->obj) {
    // create object with id and deserialize
    oproxy->obj = ostore_->create(a->object_type().c_str());
    // data from buffer into object
    serializer_.deserialize(oproxy->obj, *buffer_, ostore_);
    // insert object
    ostore_->insert_object(oproxy->obj, false);
  } else {
    // data from buffer into object
    serializer_.deserialize(oproxy->obj, *buffer_, ostore_);
  }
}

transaction::iterator action_inserter::insert(object *o)
{
  obj_ = o;
  inserted_ = false;
  transaction::iterator first = action_list_.begin();
  transaction::iterator last = action_list_.end();
  while (first != last) {
    (*first)->accept(this);
    if (inserted_) {
      return first;
    } else {
      ++first;
    }
  }
  if (!inserted_) {
    insert_action *a = new insert_action(obj_->object_type());
    a->push_back(o);
    return action_list_.insert(action_list_.end(), a);
  }
  return last;
}

void action_inserter::visit(insert_action *a)
{
  // check (object) type of insert action
  // if type is equal to objects type
  // add object to action
  if (a->type() == obj_->object_type()) {
    a->push_back(obj_);
    inserted_ = true;
  }
}

void action_inserter::visit(update_action *a)
{
  // error: object can't be updated before
  // it is inserted, throw error
}

void action_inserter::visit(delete_action *a)
{
  // error: object can't be deleted before
  // it is inserted, throw error
}

bool action_remover::remove(transaction::iterator i, object *o)
{
  obj_ = o;
  iter_ = i;
  (*i)->accept(this);
  obj_ = 0;
  return true;
}

void action_remover::visit(insert_action *a)
{
  /***********
   * 
   * an insert action was found
   * try to find object with given
   * id and delete and remove it
   * from insert action
   *
   ***********/
  insert_action::iterator i = a->find(id_);
  if (i != a->end()) {
    a->erase(i);
  }
  if (a->empty()) {
    delete a;
    action_list_.erase(iter_);
  }
}

void action_remover::visit(update_action *a)
{
  /***********
   * 
   * an update action was found
   * replace this update action
   * with a new delete action
   * with this given object.
   *
   ***********/
  if (a->obj()->id() == id_) {
    *iter_ = new delete_action(a->obj());
    delete a;
  }
}

void action_remover::visit(delete_action *a)
{
  /***********
   * 
   * an delete action was found
   * this is an error, there can't
   * be a second delete action for
   * the same object
   * throw an error
   * 
   ***********/
  if (a->id() == id_) {
    // ERROR: object was deleted twice
    // throw exception
  }
}

}
