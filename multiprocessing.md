# Multi-Processing Approach

Goal is to be able to launch n replicas of a process with each
- own "view" of cwd with process-specific fifo pipes
- share a single kafka consumer/producer for all topics

For filesystem view, portable solution is to create a temporary cwd
for each process with symlinks to real cwd for all files, plus owned fifos

This means that:
- process manager needs to handle setting up of process "namespace", creating fifos
and launching their pthreads 
- process manager needs a handle to kafka manager to pass to the threads

Let's make this a more generic `nosdk_io_mgr` because we will have IOs with similar patterns
that process manager will have to set up

For now let's still create one consumer per topic. Producers works easily with this model

For consumers, each fifo pthread still needs to block on opening the fifo for writing

Once it's open:
- calls a new function `nosdk_io_mgr_kafka_consume(mgr, topic)` to get a message
- this locks the topic consumer until we call `nosdk_io_mgr_kafka_commit(mgr, msg)`
  - or `nosdk_io_mgr_kafka_cancel`

## Group IDs

To truly support Kafka semantics we also have to support multiple consumer groups.
So we will need one consumer for each combination of topic/groupid. Yikes!

Why can't we share consumers across topics? The issue in my mind is that, the user may
request to consume a number of topics, but we don't know what their processing rate will
be for those topics. So if we queue messages internally, and we have a message waiting
for topic A, further messages for A will just have to be thrown away until it's processed

## Batching

This might be suboptimal because we are only reading one message per call to poll,
and we are locking until we can write it out to a connected process. What if we could 
batch?

Manager would have to temporarily store the batch of messages, dispatching them to
process consumers when requested, tracking which have not been committed, and have a
timeout to call poll again if they are not all consumed by a process

Complicated but we have to do it to be performant. Get it working with locking and poll
first, then batching can be added as an optimization within the kafka manager without
changing the interface.

# TODO

Get all of the above working with a single process. New command syntax is:

nosdk --subscribe foo --publish bar -n 5 -- python my_app.py

To launch 5 copies of `python my_app.py`

Only allow one command to be launched from command line. Multiple commands
requires a config file

The program will
- set up all requested producers/consumers and add them to the io manager
- add the process spec to the process manager
- launch the process in their own namespaces
- format the process output nicely like:

[nosdk: python-1] hello

Start by converting the list of "kafkas" to the new io_mgr component

Then do the namespace symlink magic to make sure that works before sharing consumers
```
