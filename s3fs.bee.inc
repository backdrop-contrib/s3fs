<?php

/**
 * @file
 * Defines a bee command that refreshes the S3 metadata cache.
 */

/**
 * Implements hook_bee_command().
 */
function s3fs_bee_command() {
  $items = array();
  $items['s3fs-refresh-cache'] = array(
    'description' => bt('Refresh the S3 File System metadata cache.'),
    'bootstrap' => BACKDROP_BOOTSTRAP_FULL,
    'aliases' => array('s3fs-rc'),
    'callback' => 'bee_s3fs_refresh_cache',
  );
  $items['s3fs-copy-local'] = array(
    'description' => bt('Copy local files from your public and/or private file system(s) into your S3 bucket, if s3fs is configured to take them over.'),
    'bootstrap' => BACKDROP_BOOTSTRAP_FULL,
    'aliases' => array('s3fs-cl'),
    'callback' => 'bee_s3fs_copy_local',
  );
  return $items;
}

/**
 * Refreshes the file metadata cache.
 */
function bee_s3fs_refresh_cache() {
  $config = config('s3fs.settings');
  if (!_s3fs_validate_config($config)) {
    bee_message(
      bt('Unable to validate your s3fs configuration settings. Please configure S3 File System from the admin/config/media/s3fs page and try again.'),
      'error'
    );
    return;
  }
  _s3fs_refresh_cache($config);
}

/**
 * Copies all files from the local public/private filesystem folders into S3,
 * if s3fs is configured to take over those systems.
 */
function bee_s3fs_copy_local() {
  $config = config('s3fs.settings');

 if (!_s3fs_validate_config($config)) {
    bee_message(
      bt('Unable to validate your s3fs configuration settings. Please configure S3 File System from the admin/config/media/s3fs page and try again.'),
      'error'
    );
    return; }

  if (empty($config->get('s3fs_use_s3_for_public')) && empty($config->get('s3fs_use_s3_for_private'))) {
    bee_message(
      bt('S3 File System is not configured to take over any other file systems.')
    );
    return;
  }


  if (!empty($config->get('s3fs_use_s3_for_public'))) {
    _s3fs_copy_file_system_to_s3('public');
  }
  if (!empty($config->get('s3fs_use_s3_for_private'))) {
    _s3fs_copy_file_system_to_s3('private');
  }
}
