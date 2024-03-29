<?php

/**
 * @file
 * This file contains no working PHP code; it exists to provide additional
 * documentation for doxygen as well as to document hooks in the standard
 * Backdrop manner.
 */

/**
 * @defgroup s3fs_hooks S3 File System hooks
 * Hooks that can be implemented by other modules to extend S3 File System.
 */

/**
 * Alters the format and options used when creating an external URL.
 *
 * For example the URL can be a URL directly to the file, or can be a URL to a
 * torrent. In addition, it can be authenticated (time limited), and in that
 * case a save-as can be forced.
 *
 * @param array $url_settings
 *   Associative array of URL settings:
 *     - 'torrent': (boolean) Should the file should be sent via BitTorrent?
 *     - 'presigned_url': (boolean) Triggers use of an authenticated URL.
 *     - 'timeout': (int) Time in seconds before a pre-signed URL times out.
 *     - 'api_args': array of additional arguments to the getObject() function:
 *       https://docs.aws.amazon.com/aws-sdk-php/v3/api/api-s3-2006-03-01.html#getobject
 *     - 'custom_GET_args': (array) Implementing this hook allows you to add
 *       your own set of custom GET arguments to the S3 URLs of your files.
 *       If your custom args' keys start with "x-", S3 will ignore them, but
 *       still log them:
 *       http://docs.aws.amazon.com/AmazonS3/latest/dev/LogFormat.html#LogFormatCustom
 *
 * @param string $s3_file_path
 *   The path to the file within your S3 bucket. This includes the prefixes
 *   which might be added, e.g. the Public Folder and/or Root Folder settings.
 *
 * @return array
 *   The modified array of configuration items.
 */
function hook_s3fs_url_settings_alter(&$url_settings, $s3_file_path) {
  // An example of what you might want to do with this hook.
  if ($s3_file_path == 'myfile.jpg') {
    $url_settings['presigned_url'] = TRUE;
    $url_settings['timeout'] = 10;
  }

  // An example of adding a custom GET argument to all S3 URLs that
  // records the name of the currently logged in user.
  global $user;
  $url_settings['custom_GET_args']['x-user'] = $user->name;
}

/**
 * Alters the S3 file parameters when uploading an object.
 *
 * @param array $upload_params
 *   Associative array of upload settings
 * @see https://docs.aws.amazon.com/aws-sdk-php/v3/api/api-s3-2006-03-01.html#putobject
 */
function hook_s3fs_upload_params_alter(&$upload_params) {
  // An example of what you might want to do with this hook.
  if (strpos($upload_params['Key'], 'private/') !== FALSE) {
    $upload_params['ACL'] = 'private';
  }
}
