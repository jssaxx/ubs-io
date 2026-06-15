# Usage Example
```
import pykvc

ret = pykvc.initialize(device_id=0, ssd_size=0)
assert(ret == 0)

value = bytes(b"hello")
ret = pykvc.put("key", value)
assert(ret == 0)

value = bytes(5)
ret = pykvc.get("key", value)
assert(ret == 0)
print(value)

ret = pykvc.exist("key")
assert(ret == 0)

ret = pykvc.delete("key")
assert(ret == 0)

length = pykvc.get_length("key")
assert(length != 0)
print(length)


keys = ["key1", "key2", "key3"]
values = [b"value1" * 1024, b"value2" * 2 * 1024, b"value3" * 3 * 1024]

ret = pykvc.batch_put(keys, values)
print(ret)

ret = pykvc.batch_exist(keys)
print(ret)

values = [bytes(6 * 1024), bytes(2 * 6 * 1024), bytes(3 * 6 * 1024)]
ret = pykvc.batch_get(keys, values)
print(ret)

lengths = pykvc.batch_get_length(keys)
print(lengths) 

ret = pykvc.batch_delete(keys)
print(ret)

pykvc.exit()

```
