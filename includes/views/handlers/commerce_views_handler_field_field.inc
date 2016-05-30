<?php

/**
 * @file
 * Contains a basic implementation of handler_filed_field with lock.
 */
class commerce_views_handler_field_field extends views_handler_field_field {

  /**
   * Load the entities for all fields that are about to be displayed.
   */
  function post_execute(&$values) {
    $conditions = array();

    // If this field was loaded from a CommerceOrderViewsQuery, respect the lock
    // loading setting.
    if ($this->query instanceof CommerceViewsQuery) {
      // For non-locking Order loading, we need to pass the "_lock" condition.
      $conditions = array('_lock' => !empty($this->query->options['commerce_lock_items']));
    }

    // The following code is basically a copy-and-paste from the default
    // views_handler_field_field::post_execute() code. The difference is that
    // here we handle the "_lock" condition when loading Order instances.
    if (!empty($values)) {
      // Divide the entity ids by entity type, so they can be loaded in bulk.
      $entities_by_type = array();
      $revisions_by_type = array();
      foreach ($values as $key => $object) {
        if (isset($this->aliases['entity_type']) && isset($object->{$this->aliases['entity_type']}) && isset($object->{$this->field_alias}) && !isset($values[$key]->_field_data[$this->field_alias])) {
          $entity_type = $object->{$this->aliases['entity_type']};
          if (empty($this->definition['is revision'])) {
            $entity_id = $object->{$this->field_alias};
            $entities_by_type[$entity_type][$key] = $entity_id;
          }
          else {
            $revision_id = $object->{$this->field_alias};
            $entity_id = $object->{$this->aliases['entity_id']};
            $entities_by_type[$entity_type][$key] = array($entity_id, $revision_id);
          }
        }
      }

      // Load the entities.
      foreach ($entities_by_type as $entity_type => $entity_ids) {
        $entity_info = entity_get_info($entity_type);
        if (empty($this->definition['is revision'])) {
          $entities = entity_load($entity_type, $entity_ids, $conditions);
          $keys = $entity_ids;
        }
        else {
          // Revisions can't be loaded multiple, so we have to load them
          // one by one.
          $entities = array();
          $keys = array();
          foreach ($entity_ids as $key => $combined) {
            list($entity_id, $revision_id) = $combined;
            // Adding the revision as a condition to the previous ones.
            $conditions[$entity_info['entity keys']['revision']] = $revision_id;
            $entity = entity_load($entity_type, array($entity_id), $conditions);
            if ($entity) {
              $entities[$revision_id] = array_shift($entity);
              $keys[$key] = $revision_id;
            }
          }
        }

        foreach ($keys as $key => $entity_id) {
          // If this is a revision, load the revision instead.
          if (isset($entities[$entity_id])) {
            $values[$key]->_field_data[$this->field_alias] = array(
              'entity_type' => $entity_type,
              'entity' => $entities[$entity_id],
            );
          }
        }
      }

      // Now, transfer the data back into the resultset so it can be easily used.
      foreach ($values as $row_id => &$value) {
        $value->{'field_' . $this->options['id']} = $this->set_items($value, $row_id);
      }
    }
  }
}
