# Feature enabled but not used

Statistics        Avg      Stdev        Max
  Reqs/sec      1610.85    1570.93   10162.35
  Latency       75.48ms    21.70ms   448.19ms
  HTTP codes:
    1xx - 0, 2xx - 49681, 3xx - 0, 4xx - 0, 5xx - 0
    others - 0
  Throughput:     1.61GB/s

# Feature on just route entry
```json
[
  {
    "assemblyName": "Mvc",
    "typeName": "Benchmarks.Controllers.JsonController",
    "methodName": "JsonNk"
  }
]
```

Statistics        Avg      Stdev        Max
  Reqs/sec      1086.95    1719.28   20057.05
  Latency      135.55ms    57.17ms      1.50s
  HTTP codes:
    1xx - 0, 2xx - 27721, 3xx - 0, 4xx - 0, 5xx - 0
    others - 0
  Throughput:     0.90GB/s