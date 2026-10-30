<?php

/**
 * @file
 * Deprecated S3 client backed by the bundled AWS SDK for PHP.
 */

/**
 * S3fsClient's operations on the AWS SDK for PHP.
 *
 * Used only while the AWS Connect module is not enabled, so sites that update
 * S3 File System without adding AWS Connect keep working. The next major
 * release removes this class and the bundled SDK.
 *
 * @deprecated Enable the AWS Connect module; S3fsClient replaces this class.
 */
class S3fsSdkClient {

  /**
   * Constructs an S3fsSdkClient.
   *
   * @param \Aws\S3\S3Client $client
   *   The SDK client.
   * @param string $bucket
   *   The bucket name.
   */
  public function __construct(
    protected $client,
    protected string $bucket,
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
   *   If the AWS SDK could not be loaded.
   */
  public static function fromConfig($config): static {
    $library = _s3fs_load_awssdk_library();
    if (!$library['loaded'] || !class_exists('Aws\\S3\\S3Client')) {
      throw new S3fsException(t('Unable to load the AWS SDK. Enable the AWS Connect module, which S3 File System uses instead of the SDK.'));
    }

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

    $client_config = [];
    // If we have configured credentials locally use them, otherwise let
    // the SDK find them per API docs.
    if (_s3fs_config_get_value($config, 'use_instance_profile')) {
      $credentials_file = _s3fs_config_get_value($config, 'credentials_file');
      $provider = $credentials_file
        ? \Aws\Credentials\CredentialProvider::ini(NULL, $credentials_file)
        : \Aws\Credentials\CredentialProvider::instanceProfile();
      $client_config['credentials'] = \Aws\Credentials\CredentialProvider::memoize($provider);
    }
    elseif (!empty($access_key) && !empty($secret_key)) {
      $client_config['credentials'] = [
        'key'    => $access_key,
        'secret' => $secret_key,
      ];
    }
    $region = _s3fs_config_get_value($config, 'region');
    if (!empty($region)) {
      $client_config['region'] = $region;
      $client_config['signature'] = 'v4';
    }
    $hostname = _s3fs_config_get_value($config, 'hostname');
    if (_s3fs_config_get_value($config, 'use_customhost') && !empty($hostname)) {
      $client_config['use_path_style_endpoint'] = FALSE;
      $client_config['endpoint'] = $hostname;
    }
    $client_config['version'] = '2006-03-01';

    return new static(new \Aws\S3\S3Client($client_config), (string) _s3fs_config_get_value($config, 'bucket'));
  }

  /**
   * Gets the bucket name.
   */
  public function getBucket(): string {
    return $this->bucket;
  }

  /**
   * Gets the public URL of an object (no signature).
   */
  public function objectUrl(string $key): string {
    return $this->client->getObjectUrl($this->bucket, $key);
  }

  /**
   * Creates a presigned GET URL for an object.
   */
  public function presignedUrl(string $key, int $expires, array $args = []): string {
    $command = $this->client->getCommand('GetObject', ['Bucket' => $this->bucket, 'Key' => $key] + $args);
    return (string) $this->client->createPresignedRequest($command, "+$expires seconds")->getUri();
  }

  /**
   * Gets an object's access control list.
   */
  public function getObjectAcl(string $key): array {
    return $this->client->getObjectAcl(['Bucket' => $this->bucket, 'Key' => $key])->toArray();
  }

  /**
   * Whether the bucket exists and is reachable with these credentials.
   */
  public function bucketExists(): bool {
    return $this->client->doesBucketExistV2($this->bucket);
  }

  /**
   * Gets an object's metadata, or NULL if the object does not exist.
   */
  public function headObject(string $key): ?array {
    try {
      return $this->client->headObject(['Bucket' => $this->bucket, 'Key' => $key])->toArray();
    }
    catch (\Aws\Exception\AwsException $e) {
      if ($e->getStatusCode() === 404) {
        return NULL;
      }
      throw $e;
    }
  }

  /**
   * Downloads a whole object, or returns NULL if it does not exist.
   */
  public function getObject(string $key): ?string {
    try {
      return (string) $this->client->getObject(['Bucket' => $this->bucket, 'Key' => $key])['Body'];
    }
    catch (\Aws\Exception\AwsException $e) {
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
    $result = $this->client->getObject([
      'Bucket' => $this->bucket,
      'Key' => $key,
      '@http' => ['stream' => TRUE],
    ]);
    $size = $result['ContentLength'] ?? NULL;
    return [\GuzzleHttp\Psr7\StreamWrapper::getResource($result['Body']), $size !== NULL ? (int) $size : NULL];
  }

  /**
   * Uploads an object with SDK-style PutObject parameters.
   */
  public function putObject(string $key, $body, array $params = []): void {
    $this->client->putObject(['Bucket' => $this->bucket, 'Key' => $key, 'Body' => $body] + $params);
  }

  /**
   * Copies an object within the bucket, keeping its metadata.
   */
  public function copyObject(string $from, string $to, array $params = []): void {
    $source = $this->bucket . '/' . implode('/', array_map('rawurlencode', explode('/', $from)));
    $this->client->copyObject(['Bucket' => $this->bucket, 'Key' => $to, 'CopySource' => $source] + $params);
  }

  /**
   * Deletes an object. Deleting a missing object succeeds.
   */
  public function deleteObject(string $key): void {
    $this->client->deleteObject(['Bucket' => $this->bucket, 'Key' => $key]);
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
   * Lists objects, one page at a time. Same arguments as S3fsClient.
   */
  public function listObjects(array $args = []): Generator {
    $versions = !empty($args['Versions']);
    $params = ['Bucket' => $this->bucket];
    if (isset($args['Prefix'])) {
      $params['Prefix'] = $args['Prefix'];
    }
    if (isset($args['MaxKeys'])) {
      $params['MaxKeys'] = (int) $args['MaxKeys'];
    }
    foreach ($this->client->getPaginator($versions ? 'ListObjectVersions' : 'ListObjects', $params) as $result) {
      yield $versions
        ? ['Versions' => $result['Versions'] ?? []]
        : ['Contents' => $result['Contents'] ?? []];
      if (!empty($args['SinglePage'])) {
        return;
      }
    }
  }

}
