<?php

/**
 * Export Drupal Commerce to Views.
 */
/**
 * Implements hook_views_plugins
 */
function commerce_views_plugins() {
  return array(
    'query' => array(
      'commerce_views_query' => array(
        'title' => t('Commerce Query'),
        'help' => t('Query will be generated and run for Commerce related views.'),
        'handler' => 'CommerceViewsQuery',
      ),
    ),
  );
}
