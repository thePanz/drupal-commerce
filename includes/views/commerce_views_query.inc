<?php

/**
 * @file
 * Defines the default query object for Commerce Orders.
 */

/**
 * Object used to create a SELECT query for Commerce Orders
 *
 */
class CommerceViewsQuery extends views_plugin_query_default {

  /**
   * @return array
   */
  function option_definition() {
    $options = parent::option_definition();
    $options['commerce_lock_items'] = array(
      'default' => TRUE,
      'translatable' => FALSE,
      'bool' => TRUE,
    );

    return $options;
  }

  /**
   * Add settings for the ui.
   * @return array
   */
  function options_form(&$form, &$form_state) {
    parent::options_form($form, $form_state);

    $form['commerce_lock_items'] = array(
      '#title' => t('Lock Commerce items'),
      '#description' => t('When loading entities, those will be locked for further processing. Disable locking to avoid lock-timeouts or deadlocks in queries.'),
      '#type' => 'checkbox',
      '#default_value' => !empty($this->options['commerce_lock_items']),
    );

    return $form;
  }

  /**
   * Returns the entity objects for the given query results.
   *
   * Mimics the behaviour of views_plugin_query, but allows admins to define
   * if the loaded orders should be locked or not.
   *
   */
  function get_result_entities($results, $relationship = NULL) {
    // The following code is basically a copy-and-paste from the default
    // views_plugin_query_default::get_result_entities() code. The difference is
    // that here we handle the "_lock" condition when loading entity instances.
    $base_table = $this->base_table;
    $base_table_alias = $base_table;

    if (!empty($relationship)) {
      foreach ($this->view->relationship as $current) {
        if ($current->alias == $relationship) {
          $base_table = $current->definition['base'];
          $base_table_alias = $relationship;
          break;
        }
      }
    }
    $table_data = views_fetch_data($base_table);

    // Bail out if the table has not specified the entity-type.
    if (!isset($table_data['table']['entity type'])) {
      return FALSE;
    }
    $entity_type = $table_data['table']['entity type'];
    $info = entity_get_info($entity_type);
    $is_revision = !empty($table_data['table']['revision']);
    $id_alias = $this->get_field_alias($base_table_alias, $info['entity keys'][$is_revision ? 'revision' : 'id']);

    // Assemble the ids of the entities to load.
    $ids = array();
    foreach ($results as $key => $result) {
      if (isset($result->$id_alias)) {
        $ids[$key] = $result->$id_alias;
      }
    }

    if (!$is_revision) {
      // Creating the entity_load conditions given the locking strategy.
      $conditions = array('_lock' => !empty($this->options['commerce_lock_items']));


      $entities = entity_load($entity_type, $ids, $conditions);
      // Re-key the array by row-index.
      $result = array();
      foreach ($ids as $key => $id) {
        $result[$key] = isset($entities[$id]) ? $entities[$id] : FALSE;
      }
    }
    else {
      // There's no way in core to load revisions in bulk.
      $result = array();
      foreach ($ids as $key => $id) {
        if (module_exists('entity')) {
          $result[$key] = entity_revision_load($entity_type, $id);
        }
        else {
          // Otherwise this isn't supported.
          watchdog('views', 'Attempt to load a revision on an unsupported entity type @entity_type.', array('@entity_type' => $entity_type), WATCHDOG_WARNING);
        }
      }
    }

    return array($entity_type, $result);
  }
}
