<?php

/**
 * @file
 * Provides a central controller for Drupal Commerce.
 *
 * A full fork of Entity API's controller, with support for revisions.
 */

/**
 * Interface for the default Drupal Commerce entity controller.
 */
interface DrupalCommerceEntityControllerInterface extends EntityAPIControllerInterface {

  /**
   * Determines whether the provided entity is locked.
   *
   * @param object $entity
   *   The entity to check.
   *
   * @return bool
   *   True if the entity is locked, false otherwise.
   */
  public function isLocked($entity);

  /**
   * Set a lock for the provided entity.
   *
   * @param object $entity
   *   The entity to lock.
   */
  public function setLock($entity);

  /**
   * Determines whether the provided entity is cached.
   *
   * @param object $entity
   *   The entity to check.
   *
   * @return bool
   *   True if the entity is cached, false otherwise.
   */
  public function isCached($entity);

  /**
   * Request that locking be skipped.
   *
   * Actual skipping of locking may or may not be possible.
   *
   * @param bool $skip_locking
   *   The boolean indicating whether locking should be skipped.
   */
  public function requestSkipLocking($skip_locking);

  /**
   * Releases locking on all entities, use with caution.
   */
  public function releaseLocking();

  /**
   * Releases the lock on an entity.
   *
   * @param object $entity
   *   The entity to release the lock for.
   */
  public function releaseLock($entity);

}

/**
 * Default implementation of DrupalCommerceEntityControllerInterface.
 *
 * Provides base entity controller for Drupal Commerce entities.
 */
class DrupalCommerceEntityController extends DrupalDefaultEntityController implements DrupalCommerceEntityControllerInterface {

  /**
   * Stores our transaction object, necessary for pessimistic locking to work.
   *
   * @var DatabaseTransaction
   */
  protected $controllerTransaction = NULL;

  /**
   * Stores the ids of locked entities.
   *
   * Necessary for knowing when to release lock by committing the transaction.
   *
   * @var array
   */
  protected $lockedEntities = array();

  /**
   * Stores the flag for if a condition has been passed for requesting locking.
   *
   * By default, locking is always requested unless specifically set to false.
   *
   * @var bool
   */
  protected $requestLocking = TRUE;

  /**
   * Stores whether a request for skipping locking has been set.
   *
   * If locking has been requested as well it will take preference and the
   * entity load will default to locking.
   *
   * @var bool
   */
  protected $requestSkipLocking = FALSE;

  /**
   * Implements DrupalCommerceEntityControllerInterface::isLocked().
   */
  public function isLocked($entity) {
    return $this->controllerTransaction && !empty($this->lockedEntities[$entity->{$this->idKey}]);
  }

  /**
   * Implements DrupalCommerceEntityControllerInterface::setLock()
   */
  public function setLock($entity) {
    $this->setLockById($entity->{$this->idKey});
  }

  /**
   * @param $entityId
   */
  protected function setLockById($entityId) {
    if (empty($entityId)) {
      return;
    }

    $this->lockedEntities[$entityId] = TRUE;
  }

  /**
   *
   * @param array $entityIds
   *  The list of EntityIDs to be marked as locked.
   */
  protected function setLockByIds($entityIds) {
    // Flip the list of entity IDs: faster intersection with already locked ones.
    $ids = array_flip($entityIds);
    $ids = array_diff_key($ids, $this->lockedEntities);

    // Lock the entities that aren't yet locked.
    foreach(array_keys($ids) as $id) {
      $this->setLockById($id);
    }
  }

  /**
   * Implements DrupalCommerceEntityControllerInterface::isCached().
   */
  public function isCached($entity) {
    return isset($this->entityCache[$entity->{$this->idKey}]);
  }

  /**
   * Implements DrupalCommerceEntityControllerInterface::requestSkipLocking().
   */
  public function requestSkipLocking($skip_locking = TRUE) {
    $this->requestSkipLocking = $skip_locking;

    return $this;
  }

  /**
   * Implements DrupalCommerceEntityControllerInterface::releaseLock().
   */
  public function releaseLock($entity) {
    // Maintain the list of locked entities and release the lock if possible.
    unset($this->lockedEntities[$entity->{$this->idKey}]);
    $this->releaseLocks();
  }

  /**
   * Determines whether the current entity type requires locking.
   *
   * @return bool
   *   True if locking is required, false otherwise.
   */
  protected function requireLocking() {
    $enabled = isset($this->entityInfo['locking mode']) && $this->entityInfo['locking mode'] == 'pessimistic';
    $not_skipped = empty($this->requestSkipLocking);

    return $enabled && $not_skipped && $this->requestLocking;
  }

  /**
   * Checks the list of tracked locked entities, and if it's empty, commits
   * the transaction in order to remove the acquired locks.
   *
   * The transaction is not necessarily committed immediately. Drupal will
   * commit it as soon as possible given the state of the transaction stack.
   */
  protected function releaseLocks() {
    if (empty($this->lockedEntities)) {
      $this->controllerTransaction = NULL;
    }
  }

  /**
   * Removes all locking for the controller, mostly used for testing
   * if you want to stop locking and run something else, or you want to cancel locking
   * on all entities at once, although be cautious with this.
   */
  public function releaseLocking() {
    $this->controllerTransaction = NULL;
    $this->lockedEntities = array();
  }

  /**
   * Overrides DrupalDefaultEntityController::load().
   *
   * Accepts a condition of locking request, may not necessarily take effect
   * as other options can override locking. If anything conflicts, locking always
   * take precedence over not locking.
   */
  public function load($ids = array(), $conditions = array()) {
    // Lock by default if the caller didn't indicate a preference.
    $conditions += array('_lock' => TRUE);
    $this->requestLocking = $conditions['_lock'];
    unset($conditions['_lock']);

    // If locking has been required, then bypass the internal cache for any
    // entities that are not already locked.
    if ($this->requireLocking()) {
      foreach (array_diff_key(array_flip($ids), $this->lockedEntities) as $id => $value) {
        unset($this->entityCache[$id]);
      }
    }

    $entities = parent::load($ids, $conditions);

    // Lock the loaded entities, if required.
    if (!empty($entities) && $this->requireLocking()) {
      $this->setLockByIds(array_keys($entities));
    }

    return $entities;
  }

  /**
   * Override of DrupalDefaultEntityController::buildQuery().
   *
   * Handle pessimistic locking.
   */
  protected function buildQuery($ids, $conditions = array(), $revision_id = FALSE) {
    $query = parent::buildQuery($ids, $conditions, $revision_id);

    if ($this->requireLocking()) {
      // In pessimistic locking mode, we issue the load query with a FOR UPDATE
      // clause. This will block all other load queries to the loaded objects
      // but requires us to start a transaction.
      if (empty($this->controllerTransaction)) {
        $this->controllerTransaction = db_transaction();
      }

      $query->forUpdate();
    }

    return $query;
  }

  /**
   * (Internal use) Invokes a hook on behalf of the entity.
   *
   * For hooks that have a respective field API attacher like insert/update/..
   * the attacher is called too.
   */
  public function invoke($hook, $entity) {
    if (!empty($this->entityInfo['fieldable']) && function_exists($function = 'field_attach_' . $hook)) {
      $function($this->entityType, $entity);
    }

    // Invoke the hook.
    module_invoke_all($this->entityType . '_' . $hook, $entity);
    // Invoke the respective entity level hook.
    if ($hook == 'presave' || $hook == 'insert' || $hook == 'update' || $hook == 'delete') {
      module_invoke_all('entity_' . $hook, $entity, $this->entityType);
    }
    // Invoke rules.
    if (module_exists('rules')) {
      rules_invoke_event($this->entityType . '_' . $hook, $entity);
    }
  }

  /**
   * Delete permanently saved entities.
   *
   * In case of failures, an exception is thrown.
   *
   * @param $ids
   *   An array of entity IDs.
   * @param $transaction
   *   An optional transaction object to pass thru. If passed the caller is
   *   responsible for rolling back the transaction if something goes wrong.
   */
  public function delete($ids, DatabaseTransaction $transaction = NULL) {
    $entities = $ids ? $this->load($ids) : FALSE;
    if (!$entities) {
      // Do nothing, in case invalid or no ids have been passed.
      return;
    }

    if (!isset($transaction)) {
      $transaction = db_transaction();
      $started_transaction = TRUE;
    }

    try {
      db_delete($this->entityInfo['base table'])
        ->condition($this->idKey, array_keys($entities), 'IN')
        ->execute();
      if (!empty($this->revisionKey)) {
        db_delete($this->entityInfo['revision table'])
          ->condition($this->idKey, array_keys($entities), 'IN')
          ->execute();
      }
      // Reset the cache as soon as the changes have been applied.
      $this->resetCache($ids);

      // Maintain the list of locked entities and release the lock if possible.
      foreach ($ids as $id) {
        unset($this->lockedEntities[$id]);
      }
      $this->releaseLocks();

      foreach ($entities as $id => $entity) {
        $this->invoke('delete', $entity);
      }
      // Ignore slave server temporarily.
      db_ignore_slave();

      return TRUE;
    }
    catch (Exception $e) {
      if (!empty($started_transaction)) {
        $transaction->rollback();
        watchdog_exception($this->entityType, $e);
      }
      throw $e;
    }
  }

  /**
   * Permanently saves the given entity.
   *
   * In case of failures, an exception is thrown.
   *
   * @param $entity
   *   The entity to save.
   * @param $transaction
   *   An optional transaction object to pass thru. If passed the caller is
   *   responsible for rolling back the transaction if something goes wrong.
   *
   * @return
   *   SAVED_NEW or SAVED_UPDATED depending on the operation performed.
   */
  public function save($entity, DatabaseTransaction $transaction = NULL) {
    if (!isset($transaction)) {
      $transaction = db_transaction();
      $started_transaction = TRUE;
    }

    try {
      // Load the stored entity, if any. If this save was invoked during a
      // previous save's insert or update hook, this means the $entity->original
      // value already set on the entity will be replaced with the entity as
      // saved. This will allow any original entity comparisons in the current
      // save process to react to the most recently saved version of the entity.
      if (!empty($entity->{$this->idKey})) {
        // In order to properly work in case of name changes, load the original
        // entity using the id key if it is available.
        $entity->original = entity_load_unchanged($this->entityType, $entity->{$this->idKey});
      }

      $this->invoke('presave', $entity);

      // When saving a new revision, unset any existing revision ID so as to
      // ensure that a new revision will actually be created, then store the old
      // revision ID in a separate property for use by hook implementations.
      if (!empty($this->revisionKey) && empty($entity->is_new) && !empty($entity->revision) && !empty($entity->{$this->revisionKey})) {
        $entity->old_revision_id = $entity->{$this->revisionKey};
        unset($entity->{$this->revisionKey});
      }

      if (empty($entity->{$this->idKey}) || !empty($entity->is_new)) {
        // For new entities, create the row in the base table, then save the
        // revision.
        $op = 'insert';
        $return = drupal_write_record($this->entityInfo['base table'], $entity);
        if (!empty($this->revisionKey)) {
          drupal_write_record($this->entityInfo['revision table'], $entity);
          $update_base_table = TRUE;
        }
      }
      else {
        $op = 'update';
        $return = drupal_write_record($this->entityInfo['base table'], $entity, $this->idKey);

        if (!empty($this->revisionKey)) {
          if (!empty($entity->revision)) {
            drupal_write_record($this->entityInfo['revision table'], $entity);
            $update_base_table = TRUE;
          }
          else {
            drupal_write_record($this->entityInfo['revision table'], $entity, $this->revisionKey);
          }
        }
      }

      if (!empty($update_base_table)) {
        // Go back to the base table and update the pointer to the revision ID.
        db_update($this->entityInfo['base table'])
        ->fields(array($this->revisionKey => $entity->{$this->revisionKey}))
        ->condition($this->idKey, $entity->{$this->idKey})
        ->execute();
      }

      // Update the static cache so that the next entity_load() will return this
      // newly saved entity.
      $this->entityCache[$entity->{$this->idKey}] = $entity;

      // Maintain the list of locked entities and release the lock if possible.
      unset($this->lockedEntities[$entity->{$this->idKey}]);
      $this->releaseLock($entity);

      $this->invoke($op, $entity);

      // Ignore slave server temporarily.
      db_ignore_slave();

      // We unset the original version of the entity after the current save as
      // it no longer accurately represents the version of the entity saved in
      // the database. However, if this save was invoked during a previous
      // save's insert or update hook, this means that any hook implementations
      // executing after this save will no longer have an original version of
      // the entity to compare against. Attempting to compare against the non-
      // existent original entity in code or Rules will result in an error.
      unset($entity->original);
      unset($entity->is_new);
      unset($entity->revision);

      return $return;
    }
    catch (Exception $e) {
      if (!empty($started_transaction)) {
        $transaction->rollback();
        watchdog_exception($this->entityType, $e);
      }
      throw $e;
    }
  }

  /**
   * Create a new entity.
   *
   * @param array $values
   *   An array of values to set, keyed by property name.
   * @return
   *   A new instance of the entity type.
   */
  public function create(array $values = array()) {
    // Add is_new property if it is not set.
    $values += array('is_new' => TRUE);

    // If there is a class for this entity type, instantiate it now.
    if (isset($this->entityInfo['entity class']) && $class = $this->entityInfo['entity class']) {
      $entity = new $class($values, $this->entityType);
    }
    else {
      // Otherwise use a good old stdClass.
      $entity = (object) $values;
    }

    // Allow other modules to alter the created entity.
    drupal_alter('commerce_entity_create', $this->entityType, $entity);

    return $entity;
  }

  /**
   * Implements EntityAPIControllerInterface.
   */
  public function export($entity, $prefix = '') {
    throw new Exception('Not implemented');
  }

  /**
   * Implements EntityAPIControllerInterface.
   */
  public function import($export) {
    throw new Exception('Not implemented');
  }

  /**
   * Builds a structured array representing the entity's content.
   *
   * The content built for the entity will vary depending on the $view_mode
   * parameter.
   *
   * @param $entity
   *   An entity object.
   * @param $view_mode
   *   View mode, e.g. 'full', 'teaser'...
   * @param $langcode
   *   (optional) A language code to use for rendering. Defaults to the global
   *   content language of the current request.
   * @return
   *   The renderable array.
   */
  public function buildContent($entity, $view_mode = 'full', $langcode = NULL, $content = array()) {
    // Remove previously built content, if exists.
    $entity->content = $content;
    $langcode = isset($langcode) ? $langcode : $GLOBALS['language_content']->language;

    // Add in fields.
    if (!empty($this->entityInfo['fieldable'])) {
      $entity->content += field_attach_view($this->entityType, $entity, $view_mode, $langcode);
    }

    // Invoke hook_ENTITY_view() to allow modules to add their additions.
    rules_invoke_all($this->entityType . '_view', $entity, $view_mode, $langcode);

    // Invoke the more generic hook_entity_view() to allow the same.
    module_invoke_all('entity_view', $entity, $this->entityType, $view_mode, $langcode);

    // Remove the build array information from the entity and return it.
    $build = $entity->content;
    unset($entity->content);

    return $build;
  }

  /**
   * Generate an array for rendering the given entities.
   *
   * @param $entities
   *   An array of entities to render.
   * @param $view_mode
   *   View mode, e.g. 'full', 'teaser'...
   * @param $langcode
   *   (optional) A language code to use for rendering. Defaults to the global
   *   content language of the current request.
   * @param $page
   *   (optional) If set will control if the entity is rendered: if TRUE
   *   the entity will be rendered without its title, so that it can be embeded
   *   in another context. If FALSE the entity will be displayed with its title
   *   in a mode suitable for lists.
   *   If unset, the page mode will be enabled if the current path is the URI
   *   of the entity, as returned by entity_uri().
   *   This parameter is only supported for entities which controller is a
   *   EntityAPIControllerInterface.
   * @return
   *   The renderable array.
   */
  public function view($entities, $view_mode = '', $langcode = NULL, $page = NULL) {
    // Create a new entities array keyed by entity ID.
    $rekeyed_entities = array();

    foreach ($entities as $key => $entity) {
      // Use the entity's ID if available and fallback to its existing key value
      // if we couldn't determine it.
      if (isset($entity->{$this->idKey})) {
        $key = $entity->{$this->idKey};
      }

      $rekeyed_entities[$key] = $entity;
    }

    $entities = $rekeyed_entities;

    // If no view mode is specified, use the first one available..
    if (!isset($this->entityInfo['view modes'][$view_mode])) {
      reset($this->entityInfo['view modes']);
      $view_mode = key($this->entityInfo['view modes']);
    }

    if (!empty($this->entityInfo['fieldable'])) {
      field_attach_prepare_view($this->entityType, $entities, $view_mode);
    }

    entity_prepare_view($this->entityType, $entities);
    $langcode = isset($langcode) ? $langcode : $GLOBALS['language_content']->language;
    $view = array();

    // Build the content array for each entity passed in.
    foreach ($entities as $key => $entity) {
      $build = entity_build_content($this->entityType, $entity, $view_mode, $langcode);

      // Add default properties to the array to ensure the content is passed
      // through the theme layer.
      $build += array(
        '#theme' => 'entity',
        '#entity_type' => $this->entityType,
        '#entity' => $entity,
        '#view_mode' => $view_mode,
        '#language' => $langcode,
        '#page' => $page,
      );

      // Allow modules to modify the structured entity.
      drupal_alter(array($this->entityType . '_view', 'entity_view'), $build, $this->entityType);
      $view[$this->entityType][$key] = $build;
    }

    return $view;
  }

}
