#!/usr/bin/python

from __future__ import division

import pyx, sys, os, re

def downsample(vec, K):
    def add(a,b):
        return tuple([ae+be for ae,be in zip(a,b)])
    def avg(elts):
        return tuple([i/len(elts) for i in reduce(add,elts)])
    if K==0:
        K = max(1,len(vec)//1000)
    vec2 = []
    for i in range(0, len(vec), K):
        elts = vec[i:i+K]
        vec2.append(avg(elts))
    return vec2

def normalize(data):
    def norm(d):
        try:
            t = d[0]/data[-1][0]
        except ZeroDivisionError:
            t = 0.
        return tuple((t,) + d[1:])
    return map(norm, data)
def merge(datas):
    count = len(datas[0][0])
    def add(a,b):
        return tuple([ae+be for ae,be in zip(a,b)])
    def avg(elts):
        return tuple([i/len(elts) for i in reduce(add,elts)])
    def mergeRecords(recs):
        return avg(recs)
    def sumRecords(recs):
        if 1:
            N = len(recs)
            return tuple([x/N for x in reduce(add,recs)])
        else:
            r = reduce(add,recs)
            return (r[0]/len(recs),) + r[1:]
    def lerp(a, b, t):
        return tuple([(a0 + (b0-a0)*t) for a0,b0 in zip(a,b)])
    def getRecords(data, t):
        last = None
        records = []
        for rec in data:
            if rec[0]<t:
                last = rec
            elif rec[0]==t:
                return [rec]
            elif last:
                a = last
                b = rec
                return [lerp(a, b, (t - a[0])/(b[0]-a[0]))]
            else:
                return [rec]
        return [last]
    datas = map(normalize, datas)
    end = max([d[-1][0] for d in datas])
        
    N = 100
    output = []
    for i in range(N+1):
        merged = [mergeRecords(getRecords(data, end*i/N)) for data in datas]
        output.append(sumRecords(merged))
    return output

def main(args):
    from optparse import OptionParser
    op = OptionParser("usage: %prog [options] file-or-directory* output")
    op.add_option('','--x-axis', dest='xAxis', action='store',
                  default='instructions',
                  choices=('instructions','time','real-time','queries'),
                  help='use user time as the x axis')
    op.add_option('','--use-real-time', dest='useRealTime', action='store_true',
                  default=False,
                  help='use real time as the x axis')
    op.add_option('','--use-queries', dest='useRealTime', action='store_true',
                  default=False,
                  help='use real time as the x axis')
    op.add_option('','--downsample', dest='downsample', type='int', default=0, metavar='FACTOR',
                  help='downsample data by FACTOR (0=auto)')
    op.add_option('','--single', dest='single', type='string', default=None)
    op.add_option('','--preview', dest='preview', action='store_true', default=False,
                  help='open result after writing')
    op.add_option('','--max-instructions', dest='maxInstructions', action='store', type='int', default=None)
    op.add_option('','--merge', dest='merge', action='store', type='string', default=None)
    op.add_option('','--interleave', dest='interleave', action='store', type='string', default=None)
    op.add_option('','--ignore', dest='ignore', action='append', type='string', default=[])
    op.add_option('','--x-title', dest='xTitle', action='store', type='string', default=None)
    op.add_option('','--y-title', dest='yTitle', action='store', type='string', default=None)
    op.add_option('','--label', dest='labels', action='append', type='string', default=None)
    op.add_option('','--normalize', dest='normalize', action='store_true', default=False)
    op.add_option('','--font-scale', dest='fontScale',
                  action='store', default=None)
    op.add_option('-q','--quiet', dest='quiet',
                  action='store_true', default=False)
    opts,args = op.parse_args()

    if opts.fontScale is not None:
        pyx.unit.set(xscale=float(opts.fontScale))

    useTime = False
    if opts.xAxis == 'time':
        useTime = True
        xAxis = 4
    elif opts.xAxis == 'real-time':
        xAxis = 11
    elif opts.xAxis == 'queries':
        xAxis = 8
    elif opts.xAxis == 'instructions':
        xAxis = 0
    else:
        op.error('Invalid x axis')

    if opts.downsample:
        if opts.downsample<0:
            op.error('expected downsample factor > 0')
        downsampleRate = opts.downsample
    else:
        downsampleRate = 0

    files = args
    output = args.pop()

    def resolve(f):
        if os.path.isdir(f):
            return os.path.join(f,'run.stats')
        else:
            return f
    def notIgnored(f):
        for i in opts.ignore:
            if f.startswith(i) or '/'+i+'-' in f:
                return False
        return True
    files = filter(notIgnored, files)
    files = map(resolve, files)
    datas = [map(eval,open(file,'r')) for file in files]
    fields = [([],d[0])[isinstance(d[0][0],str)] for d in datas]
    datas = [d[isinstance(d[0][0],str):] for d in datas]
    if not opts.quiet:
        print 'Length:',map(len,datas),'Downsample:',downsampleRate

    if opts.maxInstructions is not None:
        def trimData(data):
            for i,x in enumerate(data):
                if x[0]>=opts.maxInstructions:
                    return data[:i]
            return data
        datas = map(trimData, datas)

    #for f,d in zip(files,datas):
    #    print f,d[-1][10]
    if opts.merge:
        groups = {}
        for f,d in zip(files,datas):
            m = re.match(opts.merge,f)
            if m:
                group = '-'.join(m.groups())
            else:
                group = 'ungrouped'
            groups[group] = groups.get(group,[]) + [d]
        files,datas = zip(*[(k,merge(v)) for k,v in groups.items()])
        if not opts.quiet:
            for f,d in zip(files,datas):
                print f,len(d)
    elif opts.interleave:
        if opts.normalize:
            datas = map(normalize, datas)
        groups = {}
        for f,d in zip(files,datas):
            m = re.match(opts.interleave,f)
            if m:
                group = '-'.join(m.groups())
            else:
                group = '<ungrouped>'
            groups[group] = groups.get(group,[]) + [(f,d)]
        results = []
        lists = groups.values()
        interleavedCount = len(lists)
        for l in lists:
            l.sort()
        while 1:
            any = False
            for l in lists:
                if l:
                    any = True
                    results.append(l.pop())
            if not any:
                break
        files,datas = zip(*results)
    else:
        if opts.normalize:
            datas = map(normalize, datas)
        if downsampleRate!=1:
            datas = [downsample(data, downsampleRate) for data in datas]

    if opts.single is not None:
        if opts.single=='Tr':
            index = lambda r: r[10]
        elif opts.single=='Tq':
            index = lambda r: r[13]
        elif opts.single=='BCov':
            index = lambda r: (2*r[1]+r[2])/max(1,2*r[3])
        elif opts.single=='SCov':
            index = lambda r: r[11]/max(1,(r[11]+r[12]))
        else:
            raise ValueError,opts.single
        r = zip(files,datas)
        r.sort(key=lambda (f,d): -index(d[-1]))
        files,datas = zip(*r)
        
        c = pyx.canvas.canvas()
        if len(datas)>7:
            key = None
        else:
            if opts.single=='Tr':
                keypos = "tl"
            else:
                keypos = "br"
            key = pyx.graph.key.key(pos=keypos)
        g = pyx.graph.graphxy(height=8, key=key,
                              x=pyx.graph.axis.linear(title=opts.xTitle),
                              y=pyx.graph.axis.linear(title=opts.yTitle))

        if opts.labels is not None:
            labels = opts.labels
        else:
            labels = files
            
        plots = []
        for label,data in zip(labels,datas):
            plots.append(pyx.graph.data.list([(e[xAxis],index(e)) for e in data],
                                             title=label, x=1, y=2))

        if opts.interleave:
            line = pyx.graph.style.line()
            line.defaultlineattrs = [pyx.graph.style.attr.changelist([pyx.style.linestyle.solid,
                                                                      pyx.style.linestyle.dashed])]
            style = [line]
        else:
            style = [pyx.graph.style.line()]
        g.plot(plots, style)
        c.insert(g)

        c.writetofile(output)

        if opts.preview:
            os.system('acroread %s &'%(output,))
        return
    
    c = pyx.canvas.canvas()

    firstGraphs = None
    for i,data in enumerate(datas):
        def getYAxis(idx):
            if firstGraphs is None:
                return pyx.graph.axis.linear()
            else:
                return pyx.graph.axis.linkedaxis(firstGraphs[idx].axes["y"])
        xpos = i * 17
        ypos = 22
        g1 = pyx.graph.graphxy(height=10,xpos=xpos,ypos=ypos,
                               y=getYAxis(0),
                              key=pyx.graph.key.key(pos="br"))
        extra = []
        if len(data[0])>11:
            extra = [pyx.graph.data.list([(e[xAxis],e[12]/(e[12]+e[13])) for e in data], title='Statement', x=1, y=2)]
        g1.plot(([pyx.graph.data.list([(e[xAxis],e[1]/e[3]) for e in data], title='Full', x=1, y=2),
                  pyx.graph.data.list([(e[xAxis],e[2]/e[3]) for e in data], title='Partial', x=1, y=2),
                  pyx.graph.data.list([(e[xAxis],(e[1]+e[2])/e[3]) for e in data], title='Both', x=1, y=2)]
                 +extra),
               [pyx.graph.style.line()])
        c.insert(g1)

        ypos -= 11
        g2 = pyx.graph.graphxy(height=10,xpos=xpos,ypos=ypos,
                               y=getYAxis(1),
                              key=pyx.graph.key.key(pos="br"))
        g2.plot([pyx.graph.data.list([(e[xAxis],e[5]) for e in data], title='States', x=1, y=2),
                 pyx.graph.data.list([(e[xAxis],e[6]) for e in data], title='Non-Compact States', x=1, y=2)],
               [pyx.graph.style.line()])
        c.insert(g2)

        ypos -= 11
        g3 = pyx.graph.graphxy(height=10,xpos=xpos,ypos=ypos,
                               y=getYAxis(2),
                              key=pyx.graph.key.key(pos="br"))
        plots = []
        if useTime:
            plots.append(pyx.graph.data.list([(e[xAxis],e[0]/1000) for e in data], title='Instructions (k)', x=1, y=2))
        else:
            plots.append(pyx.graph.data.list([(e[xAxis],e[4]) for e in data], title='User Time (s)', x=1, y=2))
        if len(e)>10:
            plots.append(pyx.graph.data.list([(e[xAxis],e[11]) for e in data], title='Real Time (s)', x=1, y=2))
        if len(e)>13:
            plots.append(pyx.graph.data.list([(e[xAxis],e[14]) for e in data], title='STP Time (s)', x=1, y=2))
        if len(e)>14:
            plots.append(pyx.graph.data.list([(e[xAxis],e[15]-e[14]) for e in data], title='Non-STP Solver Time (s)', x=1, y=2))
        if len(e)>15:
            plots.append(pyx.graph.data.list([(e[xAxis],e[16]-e[14]) for e in data], title='CexCache Time (s)', x=1, y=2))
        g3.plot(plots,
                [pyx.graph.style.line()])
        c.insert(g3)

        ypos -= 11
        g4 = pyx.graph.graphxy(height=10,xpos=xpos,ypos=ypos,
                               y=getYAxis(3),
                              key=pyx.graph.key.key(pos="br"))
        plots = []
        if len(e)>8:
            plots.append(pyx.graph.data.list([(e[xAxis],e[9]/10000) for e in data], title='Query Constructs (10000s)', x=1, y=2))
        g4.plot(plots+
                [pyx.graph.data.list([(e[xAxis],e[7]/(1<<20)) for e in data], title='Memory (Mb)',
                                    x=1, y=2)],
               [pyx.graph.style.line()])
        c.insert(g4)

        if max([e[8] for e in data]):
            ypos -= 11
            g5 = pyx.graph.graphxy(height=10,xpos=xpos,ypos=ypos,
                                   y=getYAxis(4),
                                   key=pyx.graph.key.key(pos="br"))
            g5.plot([pyx.graph.data.list([(e[xAxis],e[8]) for e in data], title='Queries',
                                         x=1, y=2)],
                    [pyx.graph.style.line()])               
            c.insert(g5)
        else:
            g5 = None

            # I,BFull,BPart,BTot,T,St,Mem,QTot,QCon,MObs,Treal,SCov,SUnc,QT,Ts,Tcex,Tattempt,Tsearch,Tlook,Teval,Tsolve) = data

        if len(e)>14:
            plots = []
            if len(e)>14:
                plots.append(pyx.graph.data.list([(e[xAxis],e[14]/max(1,e[11])) for e in data], title='T in STP', x=1, y=2))
            if len(e)>14:
                plots.append(pyx.graph.data.list([(e[xAxis],e[15]/max(1,e[11])) for e in data], title='T in All Solvers', x=1, y=2))
            if len(e)>14:
                plots.append(pyx.graph.data.list([(e[xAxis],(e[15]-e[14])/max(1,e[11])) for e in data], title='T in Non-STP Solvers', x=1, y=2))
            if len(e)>15:
                plots.append(pyx.graph.data.list([(e[xAxis],(e[16]-e[14])/max(1,e[11])) for e in data], title='T in CexCache', x=1, y=2))
            ypos -= 11
            g6 = pyx.graph.graphxy(height=10,xpos=xpos,ypos=ypos,
                                   y=pyx.graph.axis.linear(min=0.,max=1.),
                                   key=pyx.graph.key.key(pos="br"))
            g6.plot(plots,
                    [pyx.graph.style.line()])               
            c.insert(g6)
        else:
            g6 = None

        if firstGraphs is None:
            firstGraphs = [g1,g2,g3,g4,g5,g6]

    c.writetofile(output)

    if opts.preview:
        os.system('acroread %s &'%(output,))
    
if __name__=='__main__':
    main(sys.argv)
