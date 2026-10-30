
# S3 File System

---

S3 File System (s3fs) provides an additional file system to your Backdrop site, alongside the public and private file systems, which stores files in Amazon's Simple Storage Service (S3) (or any S3-compatible storage service). You can set your site to use S3 File System as the default, or use it only for individual fields. This functionality is designed for sites which are load-balanced across multiple servers, as the mechanism used by Backdrop's default file systems is not viable under such a configuration.

## Installation

Install this module using the official Backdrop CMS instructions

at https://docs.backdropcms.org/documentation/extend-with-modules.

Visit the configuration page under Administration > Configuration > Media > s3fs (admin/config/media/s3fs) and enter the required information.

S3 File System connects to S3 through the AWS Connect module, which signs and sends the requests. PHP needs the curl extension and allow_url_fopen. Until AWS Connect is enabled, the module falls back to the bundled AWS SDK for PHP in vendor/. That fallback is deprecated and will be removed in the next major release, which requires AWS Connect.

## Initial Setup

------------

With the code installation complete, you must now configure s3fs to use your Amazon Web Services credentials.

The preferred method is to use environment variables, the shared credentials file, or IAM role credentials (EC2 instance profile, ECS task role or EKS Pod Identity). When no keys are configured, these are tried in that order.

Configure your settings for S3 File System (including your S3 bucket name) at admin/config/media/s3fs/settings.  Add the AWS keys either in settings.php or use the Key module.

### Essential Step! Do not skip this!

With the settings saved, go to /admin/config/media/s3fs/actions to refresh the file metadata cache. This will copy the filenames and attributes for every existing file in your S3 bucket into Drupal's database. This can take a significant amount of time for very large buckets (thousands of files). If this operation times out, you can also perform it using "bee s3fs-refresh-cache".

Please keep in mind that any time the contents of your S3 bucket change without Drupal knowing about it (like if you copy some files into it manually using another tool), you'll need to refresh the metadata cache again. S3FS assumes that its cache is a canonical listing of every file in the bucket. Thus, Drupal will not be able to access any files you copied into your bucket manually until S3FS's cache learns of them. This is true of folders as well; s3fs will not be able to copy files into folders that it doesn't know about.

## How to Configure your site to use s3fs
------------
Visit the admin/config/media/file-system page and set the "Default download method" to "Amazon Simple Storage Service" -and/or- Add a field of type File, Image, etc. and set the "Upload destination" to "Amazon Simple Storage Service" in the "Field Settings" tab.

This will configure your site to store new uploaded files in S3. Files which your site creates automatically (such as
aggregated CSS) will still be stored in the server's local filesystem, because Drupal is hard-coded to use the public:// filesystem for such files.

However, s3fs can be configured to handle these files, as well. On the s3fs configuration page (admin/config/media/s3fs) you can enable the "Use S3 for public:// files" and/or "Use S3 for private:// files" options to make s3fs take over the job of the public and/or private file systems. This will cause your site to store newly uploaded/generated files from the public/private file system in S3 instead of the local file system. However, it will make any existing files in those file systems become invisible to Drupal. To remedy this, you'll need to copy those files into your S3 bucket.

You are strongly encouraged to use the bee command "bee s3fs-copy-local" to do this, as it will copy all the files into the correct subfolders in your bucket, according to your s3fs configuration, and will write them to the metadata cache. If you don't have bee, you can use the buttons provided on the S3FS Actions page (admin/config/media/s3fs/actions), though the copy operation may fail if you have a lot of files, or very large files. The bee command will cleanly handle any combination of files.

If you're using nginx rather than Apache, you probably have a config block like this:

location ~ (^/sites/.*/files/imagecache/|^/sites/default/themes/.*/includes/fonts/|^/sites/.*/files/styles/) {
  expires max;
  try_files $uri @rewrite;
}

To make s3fs's custom image derivative mechanism work, you'll need to modify
that regex it with an additional path, like so:

location ~ (^/s3/files/styles/|^/sites/.*/files/imagecache/|^/sites/default/themes/.*/includes/fonts/|^/sites/.*/files/styles/) {
  expires max;
  try_files $uri @rewrite;
}

## AWS Permissions

------------
For s3fs to be able to function, the AWS user identified by the configured
credentials should have the following User Policy set:

{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Action": [
                "s3:ListAllMyBuckets"
            ],
            "Resource": "arn:aws:s3:::*"
        },
        {
            "Effect": "Allow",
            "Action": [
                "s3:*"
            ],
            "Resource": [
                "arn:aws:s3:::<bucket_name>",
                "arn:aws:s3:::<bucket_name>/*"
            ]
        }
    ]
}

This is not the precise list of permissions necessary, but it's broad enough to allow s3fs to function while being

strict enough to restrict access to other services.

## Aggregated CSS and JS in S3

------------

If you want your site's aggregated CSS and JS files to be stored on S3, rather than the default of storing them on the webserver's local filesystem, you'll need to do two things:

1) Enable the "Use S3 for public:// files" option in the s3fs configuration, because Drupal always* puts aggregated

CSS/JS into the public:// filesystem.

2) Because of the way browsers interpret relative URLs used in CSS files, and how they restrict requests made from external javascript files, you'll need to set up your webserver as a proxy for those files.

When you've got a module like "Advanced CSS/JS Aggregation" installed, things get hairy. For now, that module is not compatible with s3fs public:// takeover.

S3FS will present all css files in the taken over public:// filesystem with the url prefix /s3fs-css/, and all

javascript files with /s3fs-js/. So you need to set up your webserver to proxy those URLs into your S3 bucket.

For Apache, add this code to the right location* in your server's config:

ProxyRequests Off
SSLProxyEngine on
<Proxy *>
    Order deny,allow
    Allow from all
</Proxy>
ProxyPass /s3fs-css/ https://YOUR-BUCKET.s3.amazonaws.com/s3fs-public/
ProxyPassReverse /s3fs-css/ https://YOUR-BUCKET.s3.amazonaws.com/s3fs-public/
ProxyPass /s3fs-js/ https://YOUR-BUCKET.s3.amazonaws.com/s3fs-public/
ProxyPassReverse /s3fs-js/ https://YOUR-BUCKET.s3.amazonaws.com/s3fs-public/

If you're using the "S3FS Root Folder" option, you'll need to insert that
folder before the /s3fs-public/ part of the target URLs. Like so:

ProxyPass /s3fs-css/ https://YOUR-BUCKET.s3.amazonaws.com/YOUR-ROOT-FOLDER/s3fs-public/
ProxyPassReverse /s3fs-css/ https://YOUR-BUCKET.s3.amazonaws.com/YOUR-ROOT-FOLDER/s3fs-public/

If you've set up a custom name for the public folder, you'll need to change the

's3fs-public' part of the URLs above to match your custom folder name.

* The "right location" is implementation-dependent. Normally, placing these lines at the bottom of your httpd.conf file

should be sufficient. However, if your site is configured to use SSL, you'll need to put these lines in the

VirtualHost settings for both your normal and SSL sites.

For nginx, add this to your server config:

location ~* ^/(s3fs-css|s3fs-js)/(.*) { set $s3_base_path 'YOUR-BUCKET.s3.amazonaws.com/s3fs-public'; set $file_path $2;
resolver 8.8.4.4 8.8.8.8 valid=300s; resolver_timeout 10s;
proxy_pass http://$s3_base_path/$file_path; }

Again, be sure to take the S3FS Root Folder setting into account, here.

The /s3fs-public/ subfolder is where s3fs stores the files from the public:// filesystem, to avoid name conflicts with files from the s3:// filesystem.

If you're using the "Use a Custom Host" option to store your files in a non-Amazon file service, you'll need to change the proxy target to the appropriate URL for your service.

Under some domain name setups, you may be able to avoid the need for proxying by having the same domain name as your site also point to your S3 bucket. If that is the case with your site, enable the "Don't rewrite CSS/JS file paths" option to prevent s3fs from prefixing the URLs for CSS/JS files.

## Known Issues

------------
Some curl libraries, such as the one bundled with MAMP, do not come with authoritative certificate files. See the

following page for details:

http://dev.soup.io/post/56438473/If-youre-using-MAMP-and-doing-something

Because of a bizarre limitation regarding MySQL's maximum index length for InnoDB tables, the maximum uri length that S3FS supports is 250 characters. That includes the full path to the file in your bucket, as the full folder path is part of the uri.

Uploads are sent as a single PutObject request, so objects are limited to 5 GB.

## Differences from Drupal 7

The ability to save the AWS keys in UI on the settings page was removed.

This needs to be done in settings.php or by using the Key module.

Advanced features have not been fully tested.

The tests probably do not work.

## Issues

------

Bugs and feature requests should be reported in [the Issue Queue](https://github.com/backdrop-contrib/s3fs/issues).

## Current Maintainers

-------------------

- [Justin Keiser](https://github.com/keiserjb).

- Seeking additional maintainers.

- This module is supported by the [Academy of Model Aeronautics](https://www.modelaircraft.org/).

## Credits

-------

- Ported to Backdrop CMS by [Justin Keiser](https://github.com/keiserjb).

- Originally written for Drupal by [Robert Rollins](https://github.com/coredumperror).

## License

-------

This project is GPL v2 software. See the LICENSE.txt file in this directory for complete text.

## Acknowledgements

Special recognition goes to justafish, author of the AmazonS3 module:

http://drupal.org/project/amazons3

S3 File System started as a fork of her great module, but has evolved dramatically since then, becoming a very different

beast. The main benefit of using S3 File System over AmazonS3 is performance, especially for image- related operations,

due to the metadata cache that is central to S3 File System's operation.
