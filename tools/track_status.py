"""Compare public best practice measurements within each selected track."""
import argparse
import json
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('tracks',nargs='+',choices=['a','b','c','d'])
args=parser.parse_args()
feed=json.loads(Path('.results/board.json').read_text())
print('Board generated:',feed['generated_at'])
for track in args.tracks:
    ranked=[]
    for team in feed['teams']:
        entry=team['tracks'].get(track,{})
        best=entry.get('best') or {}
        if entry.get('registered') and best.get('ok'):
            ranked.append((best['points'],team['team'],best))
    ranked.sort(key=lambda row:row[0],reverse=True)
    for rank,(points,team,best) in enumerate(ranked,1):
        if rank<=5 or team=='vayu':
            print(json.dumps(dict(track=track,rank=rank,team=team,points=points,
                seconds=best['T_s'],memory_bytes=best['M_bytes'],source=best['S'],
                coefficient=best['K'])))
