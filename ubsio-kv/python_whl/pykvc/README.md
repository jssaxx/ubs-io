# Usage Example
```
import pykvc

ret = pykvc.initialize()
assert(ret == 0)

value = bytes(b"hello")
ret = pykvc.put("key", value)
assert(ret == 0)

value1 = bytes(b"world")
ret = pykvc.put("key1", value1)
assert(ret == 0)

keys = ["key", "key1"]
ret = pydfc.batch_exist(keys)
print(ret)

values = [bytes(6 * 1024), bytes(2 * 6 * 1024)]
ret = pydfc.batch_get(keys, values)
print(ret)

value == values[0]
value1 == values[1]

```