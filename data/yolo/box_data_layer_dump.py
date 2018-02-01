#!/usr/bin/env python
import numpy as np
import cv2, PIL
from PIL import Image
import os, sys
sys.path.insert(0, '../../python/')
import caffe
caffe.set_device(0)
caffe.set_mode_gpu()

mean = np.require([104, 117, 123], dtype=np.float32)[:, np.newaxis, np.newaxis]

def nms(dets, thresh):
  # -------------------------
  # Pure Python NMS baseline.
  # Written by Ross Girshick
  # -------------------------
  x1 = dets[:, 0] - dets[:, 2] / 2.
  y1 = dets[:, 1] - dets[:, 3] / 2.
  x2 = dets[:, 0] + dets[:, 2] / 2.
  y2 = dets[:, 1] + dets[:, 3] / 2.
  scores = dets[:, 4]
  areas = (x2 - x1 + 1) * (y2 - y1 + 1)
  order = scores.argsort()[::-1]
  keep = []
  while order.size > 0:
    i = order[0]
    keep.append(i)
    xx1 = np.maximum(x1[i], x1[order[1:]])
    yy1 = np.maximum(y1[i], y1[order[1:]])
    xx2 = np.minimum(x2[i], x2[order[1:]])
    yy2 = np.minimum(y2[i], y2[order[1:]])
    w = np.maximum(0.0, xx2 - xx1 + 1)
    h = np.maximum(0.0, yy2 - yy1 + 1)
    inter = w * h
    ovr = inter / (areas[i] + areas[order[1:]] - inter)
    inds = np.where(ovr <= thresh)[0]
    order = order[inds + 1]
  return dets[np.require(keep), :]

def parse_result(out_blobs, i):
  image = out_blobs["data"][i]
  image = np.transpose(image, (1, 2, 0)).astype(np.uint8).copy()
  width, height, _ = image.shape
  boxes = out_blobs["label"][i].reshape((-1,5))
  boxes[:, 0] *= width
  boxes[:, 1] *= height
  boxes[:, 2] *= width
  boxes[:, 3] *= height
  return image, boxes

  #print box_values
  #img = cv2.imread('out.jpg')
  #cv2.imwrite("out.jpg", image)
  #cv2.imshow('cv2', image)
  #cv2.waitKey(0)

def show_boxes(image, boxes):
  for box in boxes:
    if box[0] <= 0.0:
      break
    x1 = int(box[0])
    y1 = int(box[1])
    x2 = int(x1 + box[2])
    y2 = int(y1 + box[3])
    cv2.rectangle(image, (x1, y1), (x2, y2), (0, 0, 255), 2)

  cv2.imshow("out", image)
  cv2.waitKey(0)


if __name__=="__main__":
  net_proto = "./box_data_layer_test.prototxt"
  net = caffe.Net(net_proto, caffe.TEST)
  for _ in range(0,100):
    out_blobs = net.forward()
    for i in range(out_blobs["data"].shape[0]):
      image, boxes = parse_result(out_blobs, i)
      show_boxes(image, boxes)

 
