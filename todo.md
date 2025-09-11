# Needed for Demo

- Postgres
  - [ ] support pre-existing columnar tables
  - [ ] postgres jsonb table update

There is currently no way to update a record in the auto-created JSON tables,
because the primary key is not accessible.

- Support user-specified ID with the "id" key in JSON payload
  - Table will be created with ID column of the appropriate type
  - All subsequent POSTs must supply a unique "id" key
  - If the key matches an existing record, it will be overwritten
- Support transparent inclusion of ID column
  - If no "id" key given, created with auto-incrementing numeric ID
  - ID injected in the json payloads?

- Kafka
  - [ ] http interface

- S3
  - [ ] http interface

- API Gateway
  - [ ] route and process external requests

- Tracing
  - [ ] generate/propagate trace IDs
  - [ ] export traces and spans
  - [ ] collect error on process exit
  - [ ] allow custom errors

- Metrics
 - [ ] Prometheus basic resource usage and processing rate

- Performance
  - [ ] evaluate performance vs. native clients
  - [ ] support keep-alive
  - [ ] support concurrent requests per-process
