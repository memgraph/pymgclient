import linecache
import os
import tracemalloc
import mgclient


def display_top(snapshot, key_type='lineno', limit=3):
    snapshot = snapshot.filter_traces((
        tracemalloc.Filter(False, "<frozen importlib._bootstrap>"),
        tracemalloc.Filter(False, "<unknown>"),
    ))
    top_stats = snapshot.statistics(key_type)

    print("Top %s lines" % limit)
    for index, stat in enumerate(top_stats[:limit], 1):
        frame = stat.traceback[0]
        filename = os.sep.join(frame.filename.split(os.sep)[-2:])
        print("#%s: %s:%s: %.1f KiB"
              % (index, filename, frame.lineno, stat.size / 1024))
        line = linecache.getline(frame.filename, frame.lineno).strip()
        if line:
            print('    %s' % line)
    other = top_stats[limit:]
    if other:
        size = sum(stat.size for stat in other)
        print("%s other: %.1f KiB" % (len(other), size / 1024))
    total = sum(stat.size for stat in top_stats)
    print("Total allocated size: %.1f KiB" % (total / 1024))


def main():
    conn = mgclient.connect(host='127.0.0.1', port=7687)
    cursor = conn.cursor()
    for _ in range(1, 10000):
        cursor.execute("""
               CREATE (n:Person {name: 'John'})-[e:KNOWS]->
                      (m:Person {name: 'Steve'});
            """)

    while True:
        tracemalloc.start()

        cursor.execute("""
                MATCH (n:Person {name: 'John'})-[e:KNOWS]->
                      (m:Person {name: 'Steve'})
                RETURN n, m, e
            """)
        cursor.fetchone()

        snapshot = tracemalloc.take_snapshot()
        display_top(snapshot)


if __name__ == "__main__":
    main()
