struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  uint bucket;      // bucket currently containing this buffer
  uint timestamp;   // order of the most recent transition to refcnt == 0
  struct buf *hnext;
  uchar data[BSIZE];
};
