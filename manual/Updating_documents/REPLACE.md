# REPLACE 

<!-- example replace -->

`REPLACE` works similar to [INSERT](Adding_documents_to_an_index/Adding_documents_to_a_real-time_index.md), but it marks the old document with the same ID as a new document as deleted before inserting a new document.

<!-- intro -->
##### SQL:
<!-- request SQL -->

```sql
REPLACE INTO products VALUES(1, "document one", 10);
```

<!-- response -->

```sql
Query OK, 1 row affected (0.00 sec)
```

<!-- intro -->
##### HTTP:

<!-- request HTTP -->

```http
POST /replace
{
  "index":"products",
  "id":1,
  "doc":
  {
	"title":"document one",
    "tag":10
  }
}
```

<!-- response HTTP -->
```http
{
  "_index":"products",
  "_id":1,
  "created":false,
  "result":"updated",
  "status":200
}
```

<!-- intro -->
##### PHP:

<!-- request PHP -->

```php
$index->replaceDocument([
   'title' => 'document one',
    'tag' => 10 
],1);
```

<!-- response PHP -->
```php
Array(
    [_index] => products
    [_id] => 1
    [created] => false
    [result] => updated
    [status] => 200
)
```

<!-- end -->

`REPLACE` is supported for RT and PQ indexes.

The old document is not removed from the index, it is only marked as deleted. Because of this the index size grows until index chunks are merged and documents marked as deleted in these chunks are not included in the chunk created as a result of merge. You can force chunk merge by using [OPTIMIZE statement](Securing_and_compacting_an_index/Compacting_an_index.md).

The syntax of the `REPLACE` statement is identical to [INSERT syntax](Adding_documents_to_an_index/Adding_documents_to_a_real-time_index.md):

```sql
REPLACE INTO index [(column1, column2, ...)]
    VALUES (value1, value2, ...)
    [, (...)]
```

`REPLACE` using HTTP protocol is performed via the `/replace` endpoint. There's also a synonym endpoint, `/index`.

<!-- example bulk replace -->

Multiple documents can be replaced at once. See [bulk adding documents](Adding_documents_to_an_index/Adding_documents_to_a_real-time_index.md#Bulk-adding-documents) for more details.

<!-- intro -->
##### HTTP:

<!-- request SQL -->

```sql
REPLACE INTO products(id,title,tag) VALUES (1, 'doc one', 10), (2,' doc two', 20);
```

<!-- response SQL -->

```sql
Query OK, 2 rows affected (0.00 sec)
```

<!-- request HTTP -->

```json
POST /bulk

{ "replace" : { "index" : "products", "id":1, "doc": { "title": "doc one", "tag" : 10 } } }
{ "replace" : { "index" : "products", "id":2, "doc": { "title": "doc two", "tag" : 20 } } }
```

<!-- response HTTP -->

```json
{
  "items":
  [
    {
      "replace":
      {
        "_index":"products",
        "_id":1,
        "created":false,
        "result":"updated",
        "status":200
      }
    },
    {
      "replace":
      {
        "_index":"products",
        "_id":2,
        "created":false,
        "result":"updated",
        "status":200
      }
    }
  ],
  "errors":false
}
```
<!-- intro -->
##### PHP:

<!-- request PHP -->

```php
$index->replaceDocuments([
    [   
        'id' => 1,
        'title' => 'document one',
        'tag' => 10 
    ],
    [   
        'id' => 2,
        'title' => 'document one',
        'tag' => 20 
    ]
);
```

<!-- response PHP -->
```php
Array(
    [items] =>
    Array(
        Array(
            [_index] => products
            [_id] => 2
            [created] => false
            [result] => updated
            [status] => 200 
        )
        Array(
            [_index] => products
            [_id] => 2
            [created] => false
            [result] => updated
            [status] => 200 
        )
    )
    [errors => false
)
```
<!-- end -->