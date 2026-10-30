<?php

/**
 * @file
 * Amazon S3 client for S3 File System, built on the AWS Connect module.
 */

/**
 * The S3 operations S3 File System needs, over signed REST calls.
 *
 * Object parameters and results use the AWS SDK for PHP names (ACL,
 * ContentType, ContentLength, LastModified, ...) so that
 * hook_s3fs_upload_params_alter() implementations and the metadata cache code
 * keep working unchanged.
 */
class S3fsClient {

  /**
   * SDK upload parameter names mapped to S3 request headers.
   */
  const PARAM_HEADERS = [
    'ACL' => 'x-amz-acl',
    'CacheControl' => 'Cache-Control',
    'ContentDisposition' => 'Content-Disposition',
    'ContentEncoding' => 'Content-Encoding',
    'ContentLanguage' => 'Content-Language',
    'ContentMD5' => 'Content-MD5',
    'ContentType' => 'Content-Type',
    'Expires' => 'Expires',
    'GrantFullControl' => 'x-amz-grant-full-control',
    'GrantRead' => 'x-amz-grant-read',
    'GrantReadACP' => 'x-amz-grant-read-acp',
    'GrantWriteACP' => 'x-amz-grant-write-acp',
    'MetadataDirective' => 'x-amz-metadata-directive',
    'ServerSideEncryption' => 'x-amz-server-side-encryption',
    'SSEKMSKeyId' => 'x-amz-server-side-encryption-aws-kms-key-id',
    'StorageClass' => 'x-amz-storage-class',
    'Tagging' => 'x-amz-tagging',
    'WebsiteRedirectLocation' => 'x-amz-website-redirect-location',
  ];

  /**
   * Constructs an S3fsClient.
   *
   * @param AwsConnectClient $client
   *   A client signing for the "s3" service in the bucket's region.
   * @param string $bucket
   *   The bucket name.
   * @param string $endpoint
   *   The service endpoint, for example "https://s3.us-east-2.amazonaws.com".
   */
  public function __construct(
    protected AwsConnectClient $client,
    protected string $bucket,
    protected string $endpoint,
  ) {}

  /**
   * Builds a client from s3fs configuration.
   *
   * @param array|Config $config
   *   Any configuration shape accepted by _s3fs_config_get_value().
   *
   * @return static
   *   The client.
   *
   * @throws S3fsException
   *   If the custom host is not a valid URL.
   */
  public static function fromConfig($config): static {
    if (module_exists('key')) {
      $ak_name = _s3fs_config_get_value($config, 'keymodule_access_key_name');
      $sk_name = _s3fs_config_get_value($config, 'keymodule_secret_key_name');
      $access_key = (is_string($ak_name) && $ak_name !== '') ? key_get_key_value($ak_name) : NULL;
      $secret_key = (is_string($sk_name) && $sk_name !== '') ? key_get_key_value($sk_name) : NULL;
      if ($access_key === NULL || $secret_key === NULL) {
        // If the keys are not set in the Key module configuration, fall back to S3FS configuration.
        $access_key = _s3fs_config_get_value($config, 'awssdk_access_key');
        $secret_key = _s3fs_config_get_value($config, 'awssdk_secret_key');
      }
    }
    else {
      $access_key = _s3fs_config_get_value($config, 'awssdk_access_key');
      $secret_key = _s3fs_config_get_value($config, 'awssdk_secret_key');
    }

    if (_s3fs_config_get_value($config, 'use_instance_profile')) {
      $credentials_file = _s3fs_config_get_value($config, 'credentials_file');
      $provider = $credentials_file
        ? AwsConnectCredentialProvider::ini($credentials_file)
        : AwsConnectCredentialProvider::instanceProfile();
    }
    elseif (!empty($access_key) && !empty($secret_key)) {
      $provider = AwsConnectCredentialProvider::fromKeys(trim($access_key), trim($secret_key));
    }
    else {
      // Environment, shared credentials file, container or instance role.
      $provider = AwsConnectCredentialProvider::defaultChain();
    }

    $region = _s3fs_config_get_value($config, 'region') ?: 'us-east-1';
    $hostname = _s3fs_config_get_value($config, 'hostname');
    if (_s3fs_config_get_value($config, 'use_customhost') && !empty($hostname)) {
      // The settings form asks for a bare hostname; accept either form.
      $endpoint = preg_match('#^https?://#i', $hostname) ? $hostname : 'https://' . $hostname;
      if (!parse_url($endpoint, PHP_URL_HOST)) {
        throw new S3fsException(t('The custom host "@host" is not a valid hostname.', ['@host' => $hostname]));
      }
    }
    else {
      $suffix = str_starts_with($region, 'cn-') ? 'amazonaws.com.cn' : 'amazonaws.com';
      $endpoint = $region === 'us-east-1' ? 'https://s3.' . $suffix : 'https://s3.' . $region . '.' . $suffix;
    }

    return new static(
      aws_connect_client('s3', $region, $provider, ['timeout' => 300]),
      (string) _s3fs_config_get_value($config, 'bucket'),
      rtrim($endpoint, '/'),
    );
  }

  /**
   * Gets the bucket name.
   */
  public function getBucket(): string {
    return $this->bucket;
  }

  /**
   * Gets the public URL of an object (no signature).
   *
   * Uses virtual-hosted style for DNS-compatible bucket names and path style
   * otherwise, like the AWS SDK.
   *
   * @param string $key
   *   The object key.
   */
  public function objectUrl(string $key): string {
    return $this->bucketUrl() . '/' . implode('/', array_map('rawurlencode', explode('/', $key)));
  }

  /**
   * Creates a presigned GET URL for an object.
   *
   * @param string $key
   *   The object key.
   * @param int $expires
   *   Lifetime in seconds, at most seven days.
   * @param array $args
   *   SDK-style GetObject Response* arguments, such as
   *   ResponseContentDisposition, to sign into the URL.
   */
  public function presignedUrl(string $key, int $expires, array $args = []): string {
    $url = $this->objectUrl($key);
    $query = [];
    foreach ($args as $name => $value) {
      // ResponseContentDisposition becomes response-content-disposition.
      $query[strtolower(preg_replace('/(?<!^)[A-Z]/', '-$0', $name))] = $value;
    }
    if ($query) {
      $url .= '?' . http_build_query($query, '', '&', PHP_QUERY_RFC3986);
    }
    return $this->client->presignUrl('GET', $url, $expires);
  }

  /**
   * Whether the bucket exists and is reachable with these credentials.
   */
  public function bucketExists(): bool {
    try {
      $this->client->request('HEAD', $this->bucketUrl() . '/');
      return TRUE;
    }
    catch (AwsConnectException $e) {
      if ($e->getStatusCode() === 404) {
        return FALSE;
      }
      throw $e;
    }
  }

  /**
   * Gets an object's access control list.
   *
   * @param string $key
   *   The object key.
   *
   * @return array
   *   SDK-style result: "Grants", each with "Grantee" (Type, ID, URI) and
   *   "Permission".
   */
  public function getObjectAcl(string $key): array {
    $xml = $this->client->request('GET', $this->objectUrl($key), ['query' => ['acl' => NULL]])->xml();
    $grants = [];
    if (isset($xml->AccessControlList->Grant)) {
      foreach ($xml->AccessControlList->Grant as $grant) {
        $type = $grant->Grantee->attributes('http://www.w3.org/2001/XMLSchema-instance')->type;
        $grants[] = [
          'Grantee' => array_filter([
            'Type' => (string) $type,
            'ID' => (string) $grant->Grantee->ID,
            'URI' => (string) $grant->Grantee->URI,
          ]),
          'Permission' => (string) $grant->Permission,
        ];
      }
    }
    return ['Grants' => $grants];
  }

  /**
   * Gets an object's metadata.
   *
   * @param string $key
   *   The object key.
   *
   * @return array|null
   *   SDK-style metadata (ContentLength, LastModified, ContentType, ETag,
   *   VersionId), or NULL if the object does not exist.
   *
   * @throws AwsConnectException
   *   On errors other than a missing object.
   */
  public function headObject(string $key): ?array {
    try {
      $response = $this->client->request('HEAD', $this->objectUrl($key));
    }
    catch (AwsConnectException $e) {
      if ($e->getStatusCode() === 404) {
        return NULL;
      }
      throw $e;
    }
    return array_filter([
      'ContentLength' => $response->getHeader('content-length') !== NULL ? (int) $response->getHeader('content-length') : NULL,
      'LastModified' => $response->getHeader('last-modified'),
      'ContentType' => $response->getHeader('content-type'),
      'ETag' => $response->getHeader('etag'),
      'VersionId' => $response->getHeader('x-amz-version-id'),
    ], fn($value) => $value !== NULL);
  }

  /**
   * Downloads a whole object into memory.
   *
   * @return string|null
   *   The object body, or NULL if the object does not exist.
   */
  public function getObject(string $key): ?string {
    try {
      return $this->client->request('GET', $this->objectUrl($key))->getBody();
    }
    catch (AwsConnectException $e) {
      if ($e->getStatusCode() === 404) {
        return NULL;
      }
      throw $e;
    }
  }

  /**
   * Opens an object for lazy reading.
   *
   * @return array
   *   The stream resource and the object size in bytes (NULL if unknown).
   */
  public function openObject(string $key): array {
    [$stream, $response] = $this->client->openStream('GET', $this->objectUrl($key));
    $length = $response->getHeader('content-length');
    return [$stream, $length !== NULL ? (int) $length : NULL];
  }

  /**
   * Uploads an object.
   *
   * @param string $key
   *   The object key.
   * @param string|resource $body
   *   The object body.
   * @param array $params
   *   SDK-style PutObject parameters (ACL, ContentType, Metadata, ...).
   */
  public function putObject(string $key, $body, array $params = []): void {
    $this->client->request('PUT', $this->objectUrl($key), [
      'body' => $body,
      'headers' => $this->paramHeaders($params),
    ]);
  }

  /**
   * Copies an object within the bucket, keeping its metadata.
   *
   * @param string $from
   *   The source key.
   * @param string $to
   *   The destination key.
   * @param array $params
   *   SDK-style CopyObject parameters (ACL, MetadataDirective, ...).
   */
  public function copyObject(string $from, string $to, array $params = []): void {
    $headers = $this->paramHeaders($params);
    $headers['x-amz-copy-source'] = '/' . rawurlencode($this->bucket) . '/' . implode('/', array_map('rawurlencode', explode('/', $from)));
    $response = $this->client->request('PUT', $this->objectUrl($to), ['headers' => $headers]);
    // S3 can report a failed copy inside a 200 response.
    if (str_contains($response->getBody(), '<Error>')) {
      $error = new AwsConnectResponse(500, $response->getHeaders(), $response->getBody());
      throw AwsConnectException::fromResponse($error);
    }
  }

  /**
   * Deletes an object. Deleting a missing object succeeds.
   */
  public function deleteObject(string $key): void {
    $this->client->request('DELETE', $this->objectUrl($key));
  }

  /**
   * Deletes every object whose key starts with a prefix.
   *
   * @return int
   *   The number of objects deleted.
   */
  public function deleteMatchingObjects(string $prefix): int {
    $count = 0;
    foreach ($this->listObjects(['Prefix' => $prefix]) as $page) {
      foreach ($page['Contents'] as $object) {
        $this->deleteObject($object['Key']);
        $count++;
      }
    }
    return $count;
  }

  /**
   * Lists objects, one page at a time.
   *
   * @param array $args
   *   Options:
   *   - Prefix: only keys starting with this prefix.
   *   - MaxKeys: page size, and the total when only one page is wanted.
   *   - Versions: list object versions instead (ListObjectVersions).
   *   - SinglePage: stop after the first page.
   *
   * @return Generator
   *   SDK-style pages: arrays with "Contents" (Key, Size, LastModified, ETag)
   *   or, for versions, "Versions" (Key, VersionId, IsLatest, Size,
   *   LastModified).
   */
  public function listObjects(array $args = []): Generator {
    $versions = !empty($args['Versions']);
    $query = $versions ? ['versions' => NULL] : ['list-type' => 2];
    if (isset($args['Prefix'])) {
      $query['prefix'] = $args['Prefix'];
    }
    if (isset($args['MaxKeys'])) {
      $query['max-keys'] = (int) $args['MaxKeys'];
    }

    do {
      $xml = $this->client->request('GET', $this->bucketUrl() . '/', ['query' => $query])->xml();
      $items = [];
      $nodes = $versions ? (isset($xml->Version) ? $xml->Version : []) : (isset($xml->Contents) ? $xml->Contents : []);
      foreach ($nodes as $item) {
        $entry = [
          'Key' => (string) $item->Key,
          'Size' => (int) $item->Size,
          'LastModified' => (string) $item->LastModified,
          'ETag' => (string) $item->ETag,
        ];
        if ($versions) {
          $entry['VersionId'] = (string) $item->VersionId;
          $entry['IsLatest'] = (string) $item->IsLatest === 'true';
        }
        $items[] = $entry;
      }
      yield [$versions ? 'Versions' : 'Contents' => $items];

      $truncated = (string) $xml->IsTruncated === 'true';
      if ($versions) {
        $query['key-marker'] = (string) $xml->NextKeyMarker;
        $query['version-id-marker'] = (string) $xml->NextVersionIdMarker;
      }
      else {
        $query['continuation-token'] = (string) $xml->NextContinuationToken;
      }
    } while ($truncated && empty($args['SinglePage']));
  }

  /**
   * Gets the bucket's base URL, without a trailing slash.
   */
  protected function bucketUrl(): string {
    $parts = parse_url($this->endpoint);
    $host = $parts['host'] . (isset($parts['port']) ? ':' . $parts['port'] : '');
    // Dotted names break TLS wildcard certificates, so they use path style.
    if (preg_match('/^[a-z0-9][a-z0-9-]{1,61}[a-z0-9]$/', $this->bucket)) {
      return $parts['scheme'] . '://' . $this->bucket . '.' . $host;
    }
    return $parts['scheme'] . '://' . $host . '/' . rawurlencode($this->bucket);
  }

  /**
   * Converts SDK-style object parameters to request headers.
   */
  protected function paramHeaders(array $params): array {
    $headers = [];
    $ignored = [];
    foreach ($params as $name => $value) {
      if ($value === NULL || $value === '' || in_array($name, ['Bucket', 'Key', 'Body'], TRUE)) {
        continue;
      }
      if ($name === 'Metadata' && is_array($value)) {
        foreach ($value as $meta_name => $meta_value) {
          $headers['x-amz-meta-' . strtolower($meta_name)] = (string) $meta_value;
        }
      }
      elseif (isset(self::PARAM_HEADERS[$name])) {
        if ($name === 'Expires' && !is_numeric($value) && ($time = strtotime((string) $value)) !== FALSE) {
          $value = gmdate('D, d M Y H:i:s \G\M\T', $time);
        }
        elseif ($name === 'Expires' && is_numeric($value)) {
          $value = gmdate('D, d M Y H:i:s \G\M\T', (int) $value);
        }
        $headers[self::PARAM_HEADERS[$name]] = (string) $value;
      }
      else {
        $ignored[] = $name;
      }
    }
    if ($ignored) {
      watchdog('s3fs', 'Ignored unsupported S3 upload parameters: @params', ['@params' => implode(', ', $ignored)], WATCHDOG_WARNING);
    }
    return $headers;
  }

}
