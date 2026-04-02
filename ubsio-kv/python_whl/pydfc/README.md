# Usage Example
```
import pydfc

ret = pydfc.initialize()
assert(ret == 0)

value = bytes(b"hello")
ret = pydfc.put("key", value)
assert(ret == 0)

value = bytes(5)
ret = pydfc.get("key", value)
assert(ret == 0)
print(value)

ret = pydfc.exist("key")
assert(ret == 0)

ret = pydfc.delete("key")
assert(ret == 0)

length = pydfc.get_length("key")
assert(length != 0)
print(length)


keys = ["key1", "key2", "key3"]
values = [b"value1" * 1024, b"value2" * 2 * 1024, b"value3" * 3 * 1024]

ret = pydfc.batch_put(keys, values)
print(ret)

ret = pydfc.batch_exist(keys)
print(ret)

values = [bytes(6 * 1024), bytes(2 * 6 * 1024), bytes(3 * 6 * 1024)]
ret = pydfc.batch_get(keys, values)
print(ret)

lengths = pydfc.batch_get_length(keys)
print(lengths) 

ret = pydfc.batch_delete(keys)
print(ret)

pydfc.exit()

```